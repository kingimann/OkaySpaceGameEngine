// Material graph: GLSL / HLSL text from the checked IR, and the own text checker.
#include "okay/Render/MaterialGraphCodegen.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace okay::matgraph {

namespace {

const char* TypeName(PinType t, ShaderLang l) {
    bool g = l == ShaderLang::GLSL;
    switch (t) {
        case PinType::Float: return "float";
        case PinType::Vec2: return g ? "vec2" : "float2";
        case PinType::Vec3: return g ? "vec3" : "float3";
        case PinType::Vec4: return g ? "vec4" : "float4";
        case PinType::Texture: return g ? "sampler2D" : "Texture2D";
        case PinType::Sampler: return g ? "int" : "SamplerState";
    }
    return "float";
}

/// Shortest text that reads back as exactly the same float, always with a '.' or exponent.
std::string Lit(float f) {
    char b[48];
    for (int prec = 1; prec <= 9; ++prec) {
        std::snprintf(b, sizeof b, "%.*g", prec, (double)f);
        if (std::strtof(b, nullptr) == f) break;
    }
    std::string s = b;
    if (s.find_first_of(".en") == std::string::npos) s += ".0";
    return s;
}

const char* kMacrosGLSL =
    "#define okm_v2(a,b) vec2(a,b)\n"
    "#define okm_v3(a,b,c) vec3(a,b,c)\n"
    "#define okm_v4(a,b,c,d) vec4(a,b,c,d)\n"
    "#define okm_splat2(x) vec2(x)\n"
    "#define okm_splat3(x) vec3(x)\n"
    "#define okm_splat4(x) vec4(x)\n"
    "#define okm_rgb(v) ((v).xyz)\n"
    "#define okm_alpha(v) ((v).w)\n"
    "#define okm_add(a,b) ((a)+(b))\n"
    "#define okm_mul(a,b) ((a)*(b))\n"
    "#define okm_lerp(a,b,t) mix(a,b,t)\n"
    "#define okm_clamp(x,lo,hi) clamp(x,lo,hi)\n"
    "#define okm_sat(x) clamp(x,0.0,1.0)\n"
    "#define okm_pow(a,b) pow(max(a,0.0),b)\n"
    "#define okm_oneminus(x) (1.0-(x))\n"
    "#define okm_dot(a,b) dot(a,b)\n"
    "#define okm_norm(v) ((v)*inversesqrt(max(dot(v,v),1e-20)))\n"
    "#define okm_fresnel(n,v,e,f0) ((f0)+(1.0-(f0))*pow(1.0-clamp(dot(n,v),0.0,1.0),e))\n"
    "#define okm_panner(uv,sp,t) ((uv)+(sp)*(t))\n"
    "#define okm_sample(t,s,uv) texture2D(t,uv)\n";

const char* kMacrosHLSL =
    "#define okm_v2(a,b) float2(a,b)\n"
    "#define okm_v3(a,b,c) float3(a,b,c)\n"
    "#define okm_v4(a,b,c,d) float4(a,b,c,d)\n"
    "#define okm_splat2(x) ((float2)(x))\n"
    "#define okm_splat3(x) ((float3)(x))\n"
    "#define okm_splat4(x) ((float4)(x))\n"
    "#define okm_rgb(v) ((v).xyz)\n"
    "#define okm_alpha(v) ((v).w)\n"
    "#define okm_add(a,b) ((a)+(b))\n"
    "#define okm_mul(a,b) ((a)*(b))\n"
    "#define okm_lerp(a,b,t) lerp(a,b,t)\n"
    "#define okm_clamp(x,lo,hi) clamp(x,lo,hi)\n"
    "#define okm_sat(x) saturate(x)\n"
    "#define okm_pow(a,b) pow(max(a,0.0),b)\n"
    "#define okm_oneminus(x) (1.0-(x))\n"
    "#define okm_dot(a,b) dot(a,b)\n"
    "#define okm_norm(v) ((v)*rsqrt(max(dot(v,v),1e-20)))\n"
    "#define okm_fresnel(n,v,e,f0) ((f0)+(1.0-(f0))*pow(1.0-saturate(dot(n,v)),e))\n"
    "#define okm_panner(uv,sp,t) ((uv)+(sp)*(t))\n"
    "#define okm_sample(t,s,uv) ((t).Sample(s,uv))\n";

// Same formulas as Hash12 / Noise2 in MaterialGraphEval.cpp.
const char* kHelpersGLSL =
    "float okm_hash(vec2 p) {\n"
    "    float a = fract(p.x * 0.7548777 + p.y * 0.5698403);\n"
    "    return fract(a * (a * 97.0 + 31.0));\n"
    "}\n"
    "float okm_noise(vec2 p, float s) {\n"
    "    vec2 q = p * s;\n"
    "    vec2 i = floor(q);\n"
    "    vec2 f = q - i;\n"
    "    vec2 u = f * f * (3.0 - 2.0 * f);\n"
    "    float a = okm_hash(i);\n"
    "    float b = okm_hash(i + vec2(1.0, 0.0));\n"
    "    float c = okm_hash(i + vec2(0.0, 1.0));\n"
    "    float d = okm_hash(i + vec2(1.0, 1.0));\n"
    "    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);\n"
    "}\n";

const char* kHelpersHLSL =
    "float okm_hash(float2 p) {\n"
    "    float a = frac(p.x * 0.7548777 + p.y * 0.5698403);\n"
    "    return frac(a * (a * 97.0 + 31.0));\n"
    "}\n"
    "float okm_noise(float2 p, float s) {\n"
    "    float2 q = p * s;\n"
    "    float2 i = floor(q);\n"
    "    float2 f = q - i;\n"
    "    float2 u = f * f * (3.0 - 2.0 * f);\n"
    "    float a = okm_hash(i);\n"
    "    float b = okm_hash(i + float2(1.0, 0.0));\n"
    "    float c = okm_hash(i + float2(0.0, 1.0));\n"
    "    float d = okm_hash(i + float2(1.0, 1.0));\n"
    "    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);\n"
    "}\n";

} // namespace

EmittedShader EmitFragment(const Program& p, ShaderLang lang, const EmitOptions& opt) {
    EmittedShader r;
    const bool gl = lang == ShaderLang::GLSL;
    auto V = [](int i) { return "v" + std::to_string(i); };

    // bindings, deduplicated
    std::map<std::string, int> texIndex, paramSeen;
    std::map<int, int> smpIndex;
    std::vector<int> texOf(p.ops.size(), -1), smpOf(p.ops.size(), -1);
    for (size_t i = 0; i < p.ops.size(); ++i) {
        const IrOp& o = p.ops[i];
        if (o.op == Op::TextureObj) {
            auto it = texIndex.find(o.s);
            if (it == texIndex.end()) { it = texIndex.emplace(o.s, (int)r.textures.size()).first; r.textures.push_back({o.s, it->second}); }
            texOf[i] = it->second;
        } else if (o.op == Op::SamplerObj) {
            auto it = smpIndex.find(o.imm);
            if (it == smpIndex.end()) { it = smpIndex.emplace(o.imm, (int)r.samplers.size()).first; r.samplers.push_back({o.imm, it->second}); }
            smpOf[i] = it->second;
        } else if (o.op == Op::Param && !paramSeen.count(o.s)) {
            paramSeen[o.s] = 1;
            ParamBinding pb{o.s, o.type, {o.v[0], o.v[1], o.v[2], o.v[3]}};
            r.params.push_back(pb);
        }
    }

    std::ostringstream body;
    for (size_t i = 0; i < p.ops.size(); ++i) {
        const IrOp& o = p.ops[i];
        if (o.op == Op::TextureObj || o.op == Op::SamplerObj) continue;
        std::string e;
        auto call = [&](const char* fn, std::initializer_list<int> args) {
            std::string s = std::string(fn) + "(";
            bool first = true;
            for (int a : args) { if (!first) s += ", "; first = false; s += V(a); }
            return s + ")";
        };
        int n = PinComponents(o.type);
        switch (o.op) {
            case Op::Const:
                if (n == 1) e = Lit(o.v[0]);
                else { e = "okm_v" + std::to_string(n) + "("; for (int k = 0; k < n; ++k) e += (k ? ", " : "") + Lit(o.v[k]); e += ")"; }
                break;
            case Op::Param: e = "okm_p_" + o.s; break;
            case Op::Uv: e = "okm_uv"; break;
            case Op::WorldPos: e = "okm_worldPos"; break;
            case Op::Normal: e = "okm_normal"; break;
            case Op::View: e = "okm_view"; break;
            case Op::Time: e = "okm_time"; break;
            case Op::Splat: e = call(n == 2 ? "okm_splat2" : n == 3 ? "okm_splat3" : "okm_splat4", {o.a}); break;
            case Op::Rgb: e = call("okm_rgb", {o.a}); break;
            case Op::Alpha: e = call("okm_alpha", {o.a}); break;
            case Op::Add: e = call("okm_add", {o.a, o.b}); break;
            case Op::Mul: e = call("okm_mul", {o.a, o.b}); break;
            case Op::Lerp: e = call("okm_lerp", {o.a, o.b, o.c}); break;
            case Op::Clamp: e = call("okm_clamp", {o.a, o.b, o.c}); break;
            case Op::Sat: e = call("okm_sat", {o.a}); break;
            case Op::Pow: e = call("okm_pow", {o.a, o.b}); break;
            case Op::OneMinus: e = call("okm_oneminus", {o.a}); break;
            case Op::Dot: e = call("okm_dot", {o.a, o.b}); break;
            case Op::Norm: e = call("okm_norm", {o.a}); break;
            case Op::Fresnel: e = call("okm_fresnel", {o.a, o.b, o.c, o.d}); break;
            case Op::Panner: e = call("okm_panner", {o.a, o.b, o.c}); break;
            case Op::Noise: e = call("okm_noise", {o.a, o.b}); break;
            case Op::Sample:
                e = "okm_sample(okm_tex_" + std::to_string(texOf[o.a]) + ", okm_smp_" + std::to_string(smpOf[o.b]) + ", " + V(o.c) + ")";
                break;
            default: break;
        }
        body << "    " << TypeName(o.type, lang) << ' ' << V((int)i) << " = " << e << ";\n";
    }
    r.body = body.str();

    std::ostringstream t;
    t << "// okaymat graph fragment, generated by MaterialGraphCodegen. Do not edit. UNVERIFIED on a GPU.\n";
    t << (gl ? kMacrosGLSL : kMacrosHLSL);
    if (gl) {
        for (const ParamBinding& pb : r.params) t << "uniform " << TypeName(pb.type, lang) << " okm_p_" << pb.name << ";\n";
        for (const TextureBinding& tb : r.textures) t << "uniform sampler2D okm_tex_" << tb.index << ";\n";
        for (const SamplerBinding& sb : r.samplers) t << "const int okm_smp_" << sb.index << " = " << sb.mode << ";\n";
    } else {
        if (!r.params.empty()) {
            t << "cbuffer OkmParams : register(b" << opt.paramRegister << ") {\n";
            for (const ParamBinding& pb : r.params) t << "    " << TypeName(pb.type, lang) << " okm_p_" << pb.name << ";\n";
            t << "}\n";
        }
        for (const TextureBinding& tb : r.textures) t << "Texture2D okm_tex_" << tb.index << " : register(t" << opt.firstTextureSlot + tb.index << ");\n";
        for (const SamplerBinding& sb : r.samplers) t << "SamplerState okm_smp_" << sb.index << " : register(s" << opt.firstTextureSlot + sb.index << ");\n";
    }
    t << (gl ? kHelpersGLSL : kHelpersHLSL);
    const char* f2 = TypeName(PinType::Vec2, lang); const char* f3 = TypeName(PinType::Vec3, lang);
    t << "struct OkmOut { " << f3 << " baseColor; float metallic; float roughness; " << f3 << " emissive; float opacity; " << f3 << " normal; };\n";
    t << "OkmOut " << opt.functionName << "(" << f2 << " okm_uv, " << f3 << " okm_worldPos, " << f3 << " okm_normal, " << f3
      << " okm_view, float okm_time) {\n";
    t << r.body;
    static const char* fields[kOutputPinCount] = {"baseColor", "metallic", "roughness", "emissive", "opacity", "normal"};
    t << "    OkmOut okm_o;\n";
    for (int k = 0; k < kOutputPinCount; ++k) if (p.out[k] >= 0) t << "    okm_o." << fields[k] << " = " << V(p.out[k]) << ";\n";
    t << "    return okm_o;\n}\n";
    r.text = t.str();
    return r;
}

// ---- the text checker -------------------------------------------------------------------------
namespace {

enum class TK { Ident, Number, Punct };
struct Tok { TK k; std::string s; int line; };

bool IsIdStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool IsIdChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

} // namespace

std::vector<std::string> CheckShaderText(const std::string& text, ShaderLang lang) {
    std::vector<std::string> bad;
    const bool gl = lang == ShaderLang::GLSL;
    auto problem = [&](int line, const std::string& m) { bad.push_back("line " + std::to_string(line) + ": " + m); };

    static const std::set<std::string> glTypes = {"float", "vec2", "vec3", "vec4", "int", "bool", "void", "sampler2D"};
    static const std::set<std::string> hlTypes = {"float", "float2", "float3", "float4", "int", "bool", "void", "Texture2D", "SamplerState"};
    static const std::set<std::string> glKeywords = {"uniform", "const", "return", "struct"};
    static const std::set<std::string> hlKeywords = {"cbuffer", "register", "const", "return", "struct", "static"};
    static const std::set<std::string> glBuiltins = {"fract", "floor", "mix", "clamp", "pow", "max", "min", "dot", "inversesqrt", "texture2D"};
    static const std::set<std::string> hlBuiltins = {"frac", "floor", "lerp", "clamp", "pow", "max", "min", "dot", "rsqrt", "saturate", "Sample"};
    static const std::set<std::string> glForbidden = {"float2", "float3", "float4", "lerp", "frac", "saturate", "Texture2D", "SamplerState",
                                                      "cbuffer", "rsqrt", "register", "Sample"};
    static const std::set<std::string> hlForbidden = {"vec2", "vec3", "vec4", "mix", "fract", "sampler2D", "uniform", "texture2D", "inversesqrt"};
    const auto& types = gl ? glTypes : hlTypes;
    const auto& kw = gl ? glKeywords : hlKeywords;
    const auto& builtins = gl ? glBuiltins : hlBuiltins;
    const auto& forbidden = gl ? glForbidden : hlForbidden;

    std::multimap<size_t, std::string> defs;   // token index at which a macro becomes known
    std::vector<Tok> toks;
    std::vector<char> stack;
    int line = 1;
    size_t i = 0;
    auto closerOf = [](char c) { return c == ')' ? '(' : c == ']' ? '[' : '{'; };

    while (i < text.size()) {
        char c = text[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') { while (i < text.size() && text[i] != '\n') ++i; continue; }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            size_t e = text.find("*/", i + 2);
            if (e == std::string::npos) { problem(line, "comment is not closed"); break; }
            for (size_t k = i; k < e; ++k) if (text[k] == '\n') ++line;
            i = e + 2; continue;
        }
        if (c == '#') {   // preprocessor line: check brackets, register #define names
            size_t e = text.find('\n', i);
            if (e == std::string::npos) e = text.size();
            std::string ln = text.substr(i, e - i);
            int depth = 0;
            for (char ch : ln) { if (ch == '(') ++depth; else if (ch == ')') { if (--depth < 0) break; } }
            if (depth != 0) problem(line, "unbalanced parentheses in a preprocessor line");
            for (size_t q = 0; q < ln.size();) {      // names of the other language inside macro bodies
                if (IsIdStart(ln[q])) {
                    size_t b = q; while (q < ln.size() && IsIdChar(ln[q])) ++q;
                    if (forbidden.count(ln.substr(b, q - b))) problem(line, "'" + ln.substr(b, q - b) + "' belongs to the other shader language");
                } else ++q;
            }
            std::istringstream ls(ln.substr(1)); std::string dir, name;
            ls >> dir;
            if (dir == "define") {
                ls >> name;
                size_t par = name.find('(');
                if (par != std::string::npos) name = name.substr(0, par);
                if (name.empty() || !IsIdStart(name[0])) problem(line, "#define without a name");
                else defs.emplace(toks.size(), name);
            } else if (dir != "version" && dir != "ifdef" && dir != "endif" && dir != "ifndef" && dir != "else") {
                problem(line, "unknown preprocessor directive '" + dir + "'");
            }
            i = e; continue;
        }
        if (IsIdStart(c)) {
            size_t s = i; while (i < text.size() && IsIdChar(text[i])) ++i;
            toks.push_back({TK::Ident, text.substr(s, i - s), line}); continue;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < text.size() && std::isdigit((unsigned char)text[i + 1]))) {
            size_t s = i;
            while (i < text.size() && (std::isdigit((unsigned char)text[i]) || text[i] == '.' || text[i] == 'e' || text[i] == 'E' ||
                                       ((text[i] == '+' || text[i] == '-') && (text[i - 1] == 'e' || text[i - 1] == 'E')))) ++i;
            std::string num = text.substr(s, i - s);
            char* end = nullptr; std::strtod(num.c_str(), &end);
            if (!end || *end != 0) problem(line, "malformed number '" + num + "'");
            if (i < text.size() && IsIdChar(text[i])) problem(line, "number runs into a name: '" + num + text[i] + "'");
            toks.push_back({TK::Number, num, line}); continue;
        }
        if (c == '(' || c == '[' || c == '{') stack.push_back(c);
        else if (c == ')' || c == ']' || c == '}') {
            if (stack.empty() || stack.back() != closerOf(c)) { problem(line, std::string("unmatched '") + c + "'"); stack.clear(); }
            else stack.pop_back();
        }
        if (!std::strchr("(){}[];,.:=+-*/<>", c)) problem(line, std::string("unexpected character '") + c + "'");
        toks.push_back({TK::Punct, std::string(1, c), line}); ++i;
    }
    if (!stack.empty()) problem(line, std::string("'") + stack.back() + "' is never closed");

    // names: declared before use, no tokens of the other language
    std::set<std::string> known, localTypes;
    std::set<std::string> vseen;
    for (size_t k = 0; k < toks.size(); ++k) {
        for (auto it = defs.lower_bound(k); it != defs.end() && it->first == k; ++it) known.insert(it->second);
        const Tok& t = toks[k];
        if (t.k != TK::Ident) continue;
        if (forbidden.count(t.s)) problem(t.line, "'" + t.s + "' belongs to the other shader language");
        if (k > 0 && toks[k - 1].s == ".") continue;                  // member access
        if (k >= 2 && toks[k - 2].s == "register") continue;         // register(t8)
        bool nextIsName = k + 1 < toks.size() && toks[k + 1].k == TK::Ident && !kw.count(toks[k + 1].s) &&
                          !types.count(toks[k + 1].s) && !localTypes.count(toks[k + 1].s);
        if (t.s == "struct" || t.s == "cbuffer") {
            if (nextIsName) { known.insert(toks[k + 1].s); if (t.s == "struct") localTypes.insert(toks[k + 1].s); }
            else problem(t.line, "'" + t.s + "' needs a name");
            continue;
        }
        if (kw.count(t.s) || builtins.count(t.s)) continue;
        if (types.count(t.s) || localTypes.count(t.s)) {
            if (nextIsName) {
                const std::string& nm = toks[k + 1].s;
                if (nm.size() > 1 && nm[0] == 'v' && std::isdigit((unsigned char)nm[1]) && !vseen.insert(nm).second)
                    problem(t.line, "'" + nm + "' is declared twice");
                known.insert(nm);
            }
            continue;
        }
        if (!known.count(t.s)) problem(t.line, "'" + t.s + "' is used but never declared");
    }
    return bad;
}

} // namespace okay::matgraph
