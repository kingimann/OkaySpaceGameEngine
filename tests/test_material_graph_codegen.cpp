// Material graph: shader text generation (GLSL and HLSL), the own text checker, and a differential test
// that RUNS the generated text on the CPU (a small interpreter of the generated statements) and compares it with
// the CPU evaluator on random graphs. Nothing here touches a GPU or a real shader compiler.
#include "test_framework.hpp"
#include "okay/Render/MaterialGraphCodegen.hpp"
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <sstream>

using namespace okay::matgraph;

namespace {

bool Wire(MaterialGraph& g, int from, const char* outPin, int to, const char* inPin) {
    const Node* f = g.Find(from); const Node* t = g.Find(to);
    if (!f || !t) return false;
    int fp = OutPinIndex(f->kind, outPin), tp = InPinIndex(t->kind, inPin);
    if (fp < 0 || tp < 0) return false;
    g.Connect(from, fp, to, tp);
    return true;
}

std::string Replace(std::string s, const std::string& a, const std::string& b) {
    for (size_t p = 0; (p = s.find(a, p)) != std::string::npos; p += b.size()) s.replace(p, a.size(), b);
    return s;
}
// HLSL type names -> GLSL ones (so the bodies can be compared line by line)
std::string ToGlslNames(std::string s) {
    s = Replace(s, "float2", "vec2"); s = Replace(s, "float3", "vec3"); s = Replace(s, "float4", "vec4");
    return s;
}
int Count(const std::string& s, char c) { int n = 0; for (char x : s) n += x == c; return n; }
int CountSub(const std::string& s, const std::string& sub) { int n = 0; for (size_t p = 0; (p = s.find(sub, p)) != std::string::npos; p += sub.size()) ++n; return n; }
std::set<std::string> Defines(const std::string& text) {
    std::set<std::string> r; std::istringstream in(text); std::string line;
    while (std::getline(in, line)) if (line.rfind("#define ", 0) == 0) { std::string n = line.substr(8); r.insert(n.substr(0, n.find('('))); }
    return r;
}

bool Compile(const MaterialGraph& g, Program& p) { std::vector<Diagnostic> d; return g.Compile(&p, &d); }

// Make a graph with node kind `k` wired (with simple sources) so that it reaches the Output.
struct Probe { MaterialGraph g; int node = -1; };
int Source(MaterialGraph& g, PinType t) {
    switch (t) {
        case PinType::Float: return g.AddConst(0.5f);
        case PinType::Vec2: { int n = g.AddNode(NodeKind::ConstVec2); g.Find(n)->v[0] = 0.25f; g.Find(n)->v[1] = 0.75f; return n; }
        case PinType::Vec3: return g.AddConst(0.1f, 0.2f, 0.3f);
        case PinType::Vec4: { int n = g.AddNode(NodeKind::ConstVec4); g.Find(n)->v[3] = 1; return n; }
        case PinType::Texture: { int n = g.AddNode(NodeKind::TextureObject); g.Find(n)->text = "a.png"; return n; }
        case PinType::Sampler: return g.AddNode(NodeKind::SamplerState);
    }
    return -1;
}
void Route(MaterialGraph& g, int n, int outPin, PinType t) {
    const char* op = GetNodeDef(g.Find(n)->kind).out[outPin].name;
    if (t == PinType::Float) { Wire(g, n, op, 1, "Roughness"); return; }
    if (t == PinType::Vec3) { Wire(g, n, op, 1, "BaseColor"); return; }
    if (t == PinType::Vec4) { int d = g.AddNode(NodeKind::Dot); Wire(g, n, op, d, "A"); Wire(g, n, op, d, "B"); Wire(g, d, "Result", 1, "Metallic"); return; }
    int ts = g.AddNode(NodeKind::TextureSample);
    if (t == PinType::Vec2) { Wire(g, Source(g, PinType::Texture), "Texture", ts, "Texture"); Wire(g, n, op, ts, "UV"); }
    else if (t == PinType::Texture) Wire(g, n, op, ts, "Texture");
    else { Wire(g, Source(g, PinType::Texture), "Texture", ts, "Texture"); Wire(g, n, op, ts, "Sampler"); }
    Wire(g, ts, "RGB", 1, "BaseColor");
}
Probe MakeProbe(NodeKind k) {
    Probe p; p.g = MaterialGraph::MakeDefault();
    p.node = p.g.AddNode(k);
    if (k == NodeKind::ParamFloat || k == NodeKind::ParamVec2 || k == NodeKind::ParamVec3 || k == NodeKind::ParamVec4) p.g.Find(p.node)->text = "knob";
    if (k == NodeKind::TextureObject) p.g.Find(p.node)->text = "wall.png";
    const NodeDef& d = GetNodeDef(k);
    for (size_t i = 0; i < d.in.size(); ++i) {
        const PinDef& pd = d.in[i];
        if (pd.def != DefKind::Required) continue;
        PinType t = pd.generic ? PinType::Vec3 : pd.type;
        Wire(p.g, Source(p.g, t), t == PinType::Texture ? "Texture" : t == PinType::Sampler ? "Sampler" : "Value", p.node, pd.name);
    }
    PinType ot = d.out[0].type;
    if (d.out[0].generic) ot = PinType::Vec3;
    if (k == NodeKind::Dot) ot = PinType::Float;
    Route(p.g, p.node, 0, ot);
    if (k == NodeKind::TextureSample) Wire(p.g, p.node, "A", 1, "Opacity");
    return p;
}

} // namespace

// ============================================================================================
// A tiny interpreter for the generated statements. It knows only what the generator prints:
//   TYPE vN = EXPR;      EXPR: number | name | fn(args)     and      okm_o.field = vN;
// Its maths is written separately from MaterialGraphEval.cpp (lerp as a+(b-a)*t, own pow/normalize...),
// except Noise2, which is the shared function.
// ============================================================================================
namespace {

using Val = std::vector<float>;

struct Interp {
    std::map<std::string, Val> env;
    const EmittedShader* sh = nullptr;
    const EvalInput* in = nullptr;
    std::string err;
    const char* s = nullptr;

    void Ws() { while (*s == ' ') ++s; }
    static bool IdCh(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

    Val Expr() {
        Ws();
        if (std::isdigit((unsigned char)*s) || *s == '-' || *s == '.') {
            char* e = nullptr; float f = std::strtof(s, &e);
            if (e == s) { err = "bad number"; return {0}; }
            s = e; return {f};
        }
        std::string id;
        while (IdCh(*s)) id += *s++;
        if (id.empty()) { err = std::string("unexpected '") + *s + "'"; return {0}; }
        Ws();
        if (*s != '(') {
            auto it = env.find(id);
            if (it == env.end()) { err = "unknown name " + id; return {0}; }
            return it->second;
        }
        ++s;
        std::vector<Val> a;
        Ws();
        if (*s != ')') for (;;) { a.push_back(Expr()); Ws(); if (*s == ',') { ++s; continue; } break; }
        if (*s != ')') { err = "missing )"; return {0}; }
        ++s;
        return Call(id, a);
    }

    static Val Map2(const Val& a, const Val& b, float (*f)(float, float)) {
        Val r(std::max(a.size(), b.size()));
        for (size_t i = 0; i < r.size(); ++i) r[i] = f(a[a.size() == 1 ? 0 : i], b[b.size() == 1 ? 0 : i]);
        return r;
    }
    static float Dot(const Val& a, const Val& b) { float s = 0; for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i]; return s; }

    Val Call(const std::string& f, const std::vector<Val>& a) {
        auto need = [&](size_t n) { if (a.size() != n) { err = f + ": wrong argument count"; return false; } return true; };
        if (f == "okm_v2" || f == "okm_v3" || f == "okm_v4") { Val r; for (auto& x : a) r.push_back(x[0]); return r; }
        if (f == "okm_splat2" || f == "okm_splat3" || f == "okm_splat4") { if (!need(1)) return {0}; return Val(f.back() - '0', a[0][0]); }
        if (f == "okm_rgb") { if (!need(1)) return {0}; return Val(a[0].begin(), a[0].begin() + 3); }
        if (f == "okm_alpha") { if (!need(1)) return {0}; return {a[0][3]}; }
        if (f == "okm_add") { if (!need(2)) return {0}; return Map2(a[0], a[1], [](float x, float y) { return x + y; }); }
        if (f == "okm_mul") { if (!need(2)) return {0}; return Map2(a[0], a[1], [](float x, float y) { return x * y; }); }
        if (f == "okm_lerp") {
            if (!need(3)) return {0};
            Val r(a[0].size());
            for (size_t i = 0; i < r.size(); ++i) r[i] = a[0][i] + (a[1][i] - a[0][i]) * a[2][a[2].size() == 1 ? 0 : i];
            return r;
        }
        if (f == "okm_clamp") {
            if (!need(3)) return {0};
            Val r(a[0].size());
            for (size_t i = 0; i < r.size(); ++i) r[i] = std::fmin(std::fmax(a[0][i], a[1][i]), a[2][i]);
            return r;
        }
        if (f == "okm_sat") { if (!need(1)) return {0}; Val r = a[0]; for (float& x : r) x = std::fmin(std::fmax(x, 0.f), 1.f); return r; }
        if (f == "okm_pow") { if (!need(2)) return {0}; return Map2(a[0], a[1], [](float x, float y) { return std::pow(std::fmax(x, 0.f), y); }); }
        if (f == "okm_oneminus") { if (!need(1)) return {0}; Val r = a[0]; for (float& x : r) x = 1.f - x; return r; }
        if (f == "okm_dot") { if (!need(2)) return {0}; return {Dot(a[0], a[1])}; }
        if (f == "okm_norm") {
            if (!need(1)) return {0};
            float k = 1.f / std::sqrt(std::fmax(Dot(a[0], a[0]), 1e-20f));
            Val r = a[0]; for (float& x : r) x *= k; return r;
        }
        if (f == "okm_fresnel") {
            if (!need(4)) return {0};
            float d = std::fmin(std::fmax(Dot(a[0], a[1]), 0.f), 1.f);
            return {a[3][0] + (1.f - a[3][0]) * std::pow(1.f - d, a[2][0])};
        }
        if (f == "okm_panner") { if (!need(3)) return {0}; return {a[0][0] + a[1][0] * a[2][0], a[0][1] + a[1][1] * a[2][0]}; }
        if (f == "okm_noise") { if (!need(2)) return {0}; return {Noise2(a[0][0], a[0][1], a[1][0])}; }
        if (f == "okm_sample") {
            if (!need(3)) return {0};
            int ti = (int)a[0][0], si = (int)a[1][0];
            float rgba[4] = {1, 1, 1, 1};
            if (in->texture) in->texture(sh->textures[ti].path, sh->samplers[si].mode, a[2][0], a[2][1], rgba);
            return Val(rgba, rgba + 4);
        }
        err = "unknown function " + f;
        return {0};
    }

    /// Runs the body statements of `text`; returns false on any parse problem (err says which).
    bool Run(const EmittedShader& e, const EvalInput& input, MaterialOutputs& out) {
        sh = &e; in = &input; env.clear(); err.clear();
        env["okm_uv"] = {input.uv[0], input.uv[1]};
        env["okm_worldPos"] = Val(input.worldPos, input.worldPos + 3);
        env["okm_normal"] = Val(input.normal, input.normal + 3);
        env["okm_view"] = Val(input.view, input.view + 3);
        env["okm_time"] = {input.time};
        for (const ParamBinding& p : e.params) {
            Val v(p.def, p.def + PinComponents(p.type));
            auto it = input.params.find(p.name);
            if (it != input.params.end()) for (size_t k = 0; k < v.size(); ++k) v[k] = it->second[k];
            env["okm_p_" + p.name] = v;
        }
        for (const TextureBinding& t : e.textures) env["okm_tex_" + std::to_string(t.index)] = {(float)t.index};
        for (const SamplerBinding& t : e.samplers) env["okm_smp_" + std::to_string(t.index)] = {(float)t.index};
        std::istringstream lines(e.text);
        std::string line; bool inFn = false;
        std::map<std::string, Val> fields;
        while (std::getline(lines, line)) {
            if (line.rfind("OkmOut okm_material(", 0) == 0) { inFn = true; continue; }
            if (!inFn || line.rfind("    ", 0) != 0) continue;
            std::string ln = line.substr(4);
            if (ln.rfind("OkmOut okm_o;", 0) == 0 || ln.rfind("return", 0) == 0) continue;
            if (ln.rfind("okm_o.", 0) == 0) {
                size_t eq = ln.find(" = ");
                std::string field = ln.substr(6, eq - 6), var = ln.substr(eq + 3, ln.size() - eq - 4);
                if (!env.count(var)) { err = "output uses unknown " + var; return false; }
                fields[field] = env[var];
                continue;
            }
            size_t sp = ln.find(' '), eq = ln.find(" = ");
            if (sp == std::string::npos || eq == std::string::npos || ln.back() != ';') { err = "bad statement: " + ln; return false; }
            std::string name = ln.substr(sp + 1, eq - sp - 1);
            std::string rhs = ln.substr(eq + 3, ln.size() - eq - 4);
            s = rhs.c_str();
            Val v = Expr();
            Ws();
            if (!err.empty() || *s != 0) { if (err.empty()) err = "trailing text in: " + ln; return false; }
            env[name] = v;
        }
        for (const char* f : {"baseColor", "metallic", "roughness", "emissive", "opacity", "normal"})
            if (!fields.count(f)) { err = std::string("no output ") + f; return false; }
        for (int k = 0; k < 3; ++k) { out.baseColor[k] = fields["baseColor"][k]; out.emissive[k] = fields["emissive"][k]; out.normal[k] = fields["normal"][k]; }
        out.metallic = fields["metallic"][0]; out.roughness = fields["roughness"][0]; out.opacity = fields["opacity"][0];
        return true;
    }
};

// ---- random graphs ----
struct Src { int node; int pin; PinType type; };

MaterialGraph RandomGraph(std::mt19937& rng) {
    MaterialGraph g = MaterialGraph::MakeDefault();
    std::vector<Src> pool;
    auto rnd = [&](float lo, float hi) { return lo + (hi - lo) * ((rng() & 0xFFFF) / 65535.0f); };
    auto add = [&](int node) {
        const NodeDef& d = GetNodeDef(g.Find(node)->kind);
        for (size_t i = 0; i < d.out.size(); ++i) pool.push_back({node, (int)i, d.out[i].type});
    };
    auto numTypeOf = [](int comps) { return comps == 1 ? PinType::Float : comps == 2 ? PinType::Vec2 : comps == 3 ? PinType::Vec3 : PinType::Vec4; };
    auto pick = [&](std::vector<Src>& cand) -> const Src* { return cand.empty() ? nullptr : &cand[rng() % cand.size()]; };
    auto of = [&](PinType t) { std::vector<Src> c; for (auto& s : pool) if (s.type == t) c.push_back(s); return c; };
    auto ofOrFloat = [&](PinType t) { std::vector<Src> c; for (auto& s : pool) if (s.type == t || s.type == PinType::Float) c.push_back(s); return c; };
    auto link = [&](const Src& s, int to, int pin) { g.Connect(g.Find(s.node)->id, s.pin, to, pin); };

    // leaves
    int nleaf = 3 + (int)(rng() % 5);
    int uniq = 0;
    for (int i = 0; i < nleaf + 3; ++i) {
        int k = i < 3 ? i : (int)(rng() % 12);
        int n = -1;
        switch (k) {
            case 0: n = g.AddConst(rnd(-2, 2)); break;
            case 1: n = g.AddConst(rnd(-2, 2), rnd(-2, 2), rnd(-2, 2)); break;
            case 2: n = g.AddNode(NodeKind::TexCoord); break;
            case 3: n = g.AddNode(NodeKind::ConstVec2); g.Find(n)->v[0] = rnd(-2, 2); g.Find(n)->v[1] = rnd(-2, 2); break;
            case 4: n = g.AddNode(NodeKind::ConstVec4); for (int c = 0; c < 4; ++c) g.Find(n)->v[c] = rnd(-2, 2); break;
            case 5: n = g.AddParam(NodeKind::ParamFloat, "pf" + std::to_string(uniq++), rnd(-2, 2)); break;
            case 6: n = g.AddParam(NodeKind::ParamVec3, "pv" + std::to_string(uniq++), rnd(0, 1), rnd(0, 1), rnd(0, 1)); break;
            case 7: n = g.AddNode(NodeKind::Time); break;
            case 8: n = g.AddNode(NodeKind::WorldNormal); break;
            case 9: n = g.AddNode(NodeKind::ViewVector); break;
            case 10: n = g.AddNode(NodeKind::WorldPosition); break;
            default: {
                int tx = g.AddNode(NodeKind::TextureObject); g.Find(tx)->text = (rng() & 1) ? "a.png" : "dir/b.png";
                int ts = g.AddNode(NodeKind::TextureSample);
                g.Connect(tx, 0, ts, 0);
                if (rng() & 1) { int sm = g.AddNode(NodeKind::SamplerState); g.Find(sm)->v[0] = (float)(rng() % 4); g.Connect(sm, 0, ts, 1); }
                auto uvs = of(PinType::Vec2);
                if (!uvs.empty() && (rng() & 1)) link(*pick(uvs), ts, 2);
                n = ts; break;
            }
        }
        add(n);
    }
    // maths and effects
    int nops = 4 + (int)(rng() % 12);
    for (int i = 0; i < nops; ++i) {
        static const NodeKind kinds[] = {NodeKind::Add, NodeKind::Multiply, NodeKind::Lerp, NodeKind::Clamp, NodeKind::Saturate,
                                         NodeKind::Power, NodeKind::OneMinus, NodeKind::Dot, NodeKind::Normalize, NodeKind::Fresnel,
                                         NodeKind::Panner, NodeKind::Noise};
        NodeKind k = kinds[rng() % 12];
        const NodeDef& d = GetNodeDef(k);
        int n = g.AddNode(k);
        if (d.in[0].generic) {
            int comps = 1 + (int)(rng() % 4);
            if (k == NodeKind::Normalize && comps == 1) comps = 2 + (int)(rng() % 3);
            PinType T = numTypeOf(comps);
            for (size_t p = 0; p < d.in.size(); ++p) {
                bool must = d.in[p].def == DefKind::Required;
                if (!must && (rng() % 10) < 3) continue;
                auto c = (k == NodeKind::Normalize) ? of(T) : ofOrFloat(T);
                if (c.empty()) c = of(PinType::Float);
                if (c.empty()) { g.nodes.pop_back(); n = -1; break; }
                link(*pick(c), n, (int)p);
            }
        } else {
            for (size_t p = 0; p < d.in.size(); ++p) {
                if ((rng() % 10) < 3) continue;
                auto c = ofOrFloat(d.in[p].type);
                if (d.in[p].type == PinType::Float) c = of(PinType::Float);
                if (c.empty()) continue;
                link(*pick(c), n, (int)p);
            }
        }
        if (n >= 0) {
            // generic results take the common type: add to the pool with the type the checker will find
            Program tmp; std::vector<Diagnostic> dd;
            bool ok = g.Compile(&tmp, &dd);
            (void)ok;
            const NodeDef& dn = GetNodeDef(k);
            PinType t = dn.out[0].type;
            if (dn.out[0].generic) {
                // read the type back from the widest linked input
                int w = 1;
                for (const Link& l : g.links) if (l.toNode == n) { const Node* f = g.Find(l.fromNode); w = std::max(w, PinComponents(GetNodeDef(f->kind).out[l.fromPin].generic ? PinType::Float : GetNodeDef(f->kind).out[l.fromPin].type)); }
                t = numTypeOf(w);
            }
            pool.push_back({n, 0, t});
        }
    }
    // outputs
    struct O { int pin; PinType t; };
    for (O o : {O{kBaseColor, PinType::Vec3}, O{kMetallic, PinType::Float}, O{kRoughness, PinType::Float}, O{kEmissive, PinType::Vec3}, O{kOpacity, PinType::Float}, O{kNormal, PinType::Vec3}}) {
        if ((rng() % 10) < 3) continue;
        auto c = ofOrFloat(o.t);
        if (!c.empty()) link(*pick(c), 1, o.pin);
    }
    return g;
}

bool Close(float a, float b) { return std::fabs(a - b) <= 2e-3f * (1.0f + std::max(std::fabs(a), std::fabs(b))); }

} // namespace

int main() {
    RUN_SUITE("material_graph_codegen");

    // ---- exact text of a tiny graph ----
    {
        MaterialGraph g = MaterialGraph::MakeDefault();
        Program p; CHECK(Compile(g, p));
        EmittedShader gl = EmitFragment(p, ShaderLang::GLSL), hl = EmitFragment(p, ShaderLang::HLSL);
        const char* wantGl =
            "    vec3 v0 = okm_v3(0.8, 0.8, 0.8);\n    float v1 = 0.0;\n    float v2 = 0.5;\n    vec3 v3 = okm_v3(0.0, 0.0, 0.0);\n"
            "    float v4 = 1.0;\n    vec3 v5 = okm_v3(0.0, 0.0, 1.0);\n";
        CHECK(gl.body == wantGl);
        CHECK(hl.body == Replace(Replace(wantGl, "vec3", "float3"), "vec2", "float2"));
        CHECK(gl.text.find("OkmOut okm_material(vec2 okm_uv, vec3 okm_worldPos, vec3 okm_normal, vec3 okm_view, float okm_time) {") != std::string::npos);
        CHECK(hl.text.find("OkmOut okm_material(float2 okm_uv, float3 okm_worldPos, float3 okm_normal, float3 okm_view, float okm_time) {") != std::string::npos);
        CHECK(gl.text.find("okm_o.baseColor = v0;") != std::string::npos);
        CHECK(gl.text.find("okm_o.normal = v5;") != std::string::npos);
        CHECK(gl.params.empty() && gl.textures.empty() && gl.samplers.empty());
        CHECK(gl.text.find("cbuffer") == std::string::npos && hl.text.find("cbuffer") == std::string::npos);   // no parameters, no buffer
        EmitOptions o; o.functionName = "my_fn";
        CHECK(EmitFragment(p, ShaderLang::GLSL, o).text.find("OkmOut my_fn(") != std::string::npos);
    }

    // ---- every node kind emits, and both languages pass the text checker ----
    {
        std::set<Op> seen;
        for (int k = 1; k < (int)NodeKind::Count; ++k) {
            Probe pr = MakeProbe((NodeKind)k);
            Program p; std::vector<Diagnostic> d;
            bool ok = pr.g.Compile(&p, &d);
            CHECK(ok);
            if (!ok) { std::cerr << "  probe failed for " << GetNodeDef((NodeKind)k).name << "\n"; for (auto& x : d) std::cerr << "    " << x.code << ": " << x.message << "\n"; continue; }
            bool reaches = false;
            for (const IrOp& o : p.ops) { reaches |= o.node == pr.node; seen.insert(o.op); }
            CHECK(reaches);
            int handles = 0; for (const IrOp& o : p.ops) handles += o.op == Op::TextureObj || o.op == Op::SamplerObj;
            for (ShaderLang l : {ShaderLang::GLSL, ShaderLang::HLSL}) {
                EmittedShader e = EmitFragment(p, l);
                CHECK(CountSub(e.body, ";\n") == (int)p.ops.size() - handles);   // one statement per op that makes a value
                auto prob = CheckShaderText(e.text, l);
                CHECK(prob.empty());
                for (auto& m : prob) std::cerr << "  " << GetNodeDef((NodeKind)k).name << ": " << m << "\n";
            }
        }
        {   // a float into a vec3 pin makes the one op the probes do not reach (Splat)
            MaterialGraph g = MaterialGraph::MakeDefault(); g.Connect(g.AddConst(0.5f), 0, 1, kBaseColor);
            Program p; CHECK(Compile(g, p)); for (const IrOp& o : p.ops) seen.insert(o.op);
            for (ShaderLang l : {ShaderLang::GLSL, ShaderLang::HLSL}) CHECK(CheckShaderText(EmitFragment(p, l).text, l).empty());
        }
        for (int o = 0; o <= (int)Op::Sample; ++o) { CHECK(seen.count((Op)o) == 1); if (!seen.count((Op)o)) std::cerr << "  op not emitted: " << OpName((Op)o) << "\n"; }
    }

    // ---- GLSL and HLSL agree structurally ----
    {
        MaterialGraph g = MaterialGraph::MakeDefault();
        int tx = g.AddNode(NodeKind::TextureObject); g.Find(tx)->text = "a.png";
        int tx2 = g.AddNode(NodeKind::TextureObject); g.Find(tx2)->text = "a.png";        // same file: one binding
        int sm = g.AddNode(NodeKind::SamplerState), sm2 = g.AddNode(NodeKind::SamplerState);   // same mode: one binding
        int sm3 = g.AddNode(NodeKind::SamplerState); g.Find(sm3)->v[0] = 3;
        int t1 = g.AddNode(NodeKind::TextureSample), t2 = g.AddNode(NodeKind::TextureSample), t3 = g.AddNode(NodeKind::TextureSample);
        Wire(g, tx, "Texture", t1, "Texture"); Wire(g, sm, "Sampler", t1, "Sampler");
        Wire(g, tx2, "Texture", t2, "Texture"); Wire(g, sm2, "Sampler", t2, "Sampler");
        Wire(g, tx, "Texture", t3, "Texture"); Wire(g, sm3, "Sampler", t3, "Sampler");
        int pa = g.AddParam(NodeKind::ParamVec3, "tint", 1, 1, 1), pb = g.AddParam(NodeKind::ParamFloat, "amount", 0.5f);
        int m1 = g.AddNode(NodeKind::Multiply), m2 = g.AddNode(NodeKind::Lerp), nz = g.AddNode(NodeKind::Noise), pn = g.AddNode(NodeKind::Panner);
        Wire(g, t1, "RGB", m1, "A"); Wire(g, pa, "Value", m1, "B");
        Wire(g, m1, "Result", m2, "A"); Wire(g, t3, "RGB", m2, "B"); Wire(g, pb, "Value", m2, "Alpha");
        Wire(g, m2, "Result", 1, "BaseColor");
        Wire(g, pn, "UV", nz, "Position"); Wire(g, nz, "Value", 1, "Roughness");
        Wire(g, t2, "A", 1, "Opacity");
        Program p; CHECK(Compile(g, p));
        EmittedShader gl = EmitFragment(p, ShaderLang::GLSL), hl = EmitFragment(p, ShaderLang::HLSL);
        CHECK(gl.body == ToGlslNames(hl.body));
        CHECK(Defines(gl.text) == Defines(hl.text));
        CHECK(gl.params.size() == 2 && hl.params.size() == 2);
        CHECK(gl.textures.size() == 1 && gl.textures[0].path == "a.png");
        CHECK(gl.samplers.size() == 2);
        int mode3 = gl.samplers[0].mode == 3 ? 0 : 1;               // order follows the graph, so look it up
        CHECK(gl.samplers[1 - mode3].mode == 0 && gl.samplers[mode3].mode == 3);
        CHECK(CountSub(gl.text, "okm_sample(okm_tex_0") == 3);     // three samples, one texture
        CHECK(CountSub(gl.text, "okm_smp_" + std::to_string(mode3)) >= 2);
        CHECK(CountSub(gl.text, "float okm_noise(") == 1 && CountSub(hl.text, "float okm_noise(") == 1);
        CHECK(CountSub(gl.text, "float okm_hash(") == 1 && CountSub(hl.text, "float okm_hash(") == 1);
        // declarations are the language's own
        CHECK(gl.text.find("uniform vec3 okm_p_tint;") != std::string::npos);
        CHECK(gl.text.find("uniform sampler2D okm_tex_0;") != std::string::npos);
        CHECK(gl.text.find("const int okm_smp_" + std::to_string(mode3) + " = 3;") != std::string::npos);
        CHECK(hl.text.find("cbuffer OkmParams : register(b5) {") != std::string::npos);
        CHECK(hl.text.find("float3 okm_p_tint;") != std::string::npos);
        CHECK(hl.text.find("Texture2D okm_tex_0 : register(t8);") != std::string::npos);
        CHECK(hl.text.find("SamplerState okm_smp_" + std::to_string(mode3) + " : register(s" + std::to_string(8 + mode3) + ");") != std::string::npos);
        CHECK(gl.text.find("texture2D(t,uv)") != std::string::npos && hl.text.find(".Sample(s,uv)") != std::string::npos);
        // balanced by plain counting (independent of the checker)
        for (const std::string* t : {&gl.text, &hl.text}) {
            CHECK(Count(*t, '{') == Count(*t, '}'));
            CHECK(Count(*t, '(') == Count(*t, ')'));
            CHECK(Count(*t, '[') == Count(*t, ']'));
            CHECK(t->find("return okm_o;") != std::string::npos);
        }
        CHECK(CheckShaderText(gl.text, ShaderLang::GLSL).empty());
        CHECK(CheckShaderText(hl.text, ShaderLang::HLSL).empty());
        // and the other language's text must NOT pass
        CHECK(!CheckShaderText(hl.text, ShaderLang::GLSL).empty());
        CHECK(!CheckShaderText(gl.text, ShaderLang::HLSL).empty());
        // options reach the text
        EmitOptions o; o.firstTextureSlot = 2; o.paramRegister = 0;
        EmittedShader h2 = EmitFragment(p, ShaderLang::HLSL, o);
        CHECK(h2.text.find("register(t2)") != std::string::npos && h2.text.find("register(b0)") != std::string::npos);
        CHECK(CheckShaderText(h2.text, ShaderLang::HLSL).empty());
    }

    // ---- the text checker catches mistakes ----
    {
        MaterialGraph g = MaterialGraph::MakeDefault();
        int c = g.AddConst(0.5f); Wire(g, c, "Value", 1, "Roughness");
        Program p; CHECK(Compile(g, p));
        std::string good = EmitFragment(p, ShaderLang::GLSL).text;
        CHECK(CheckShaderText(good, ShaderLang::GLSL).empty());
        auto breakIt = [&](const std::string& a, const std::string& b, ShaderLang l = ShaderLang::GLSL) {
            std::string t = Replace(good, a, b);
            CHECK(t != good);
            return !CheckShaderText(t, l).empty();
        };
        CHECK(breakIt("return okm_o;\n}", "return okm_o;\n"));                // missing }
        CHECK(breakIt("return okm_o;\n}", "return okm_o;\n}}"));              // extra }
        CHECK(breakIt("okm_v3(0.8, 0.8, 0.8);", "okm_v3(0.8, 0.8, 0.8;"));    // missing )
        CHECK(breakIt("okm_v3(0.8, 0.8, 0.8);", "okm_v3(0.8, 0.8, 0.8));"));
        CHECK(breakIt("okm_o.baseColor = v1;", "okm_o.baseColor = v99;"));    // undeclared
        CHECK(breakIt("okm_o.baseColor = v1;", "okm_o.baseColor = v7;"));     // declared only later? (no: never)
        CHECK(breakIt("vec3 v1 =", "vec7 v1 ="));                             // unknown type
        CHECK(breakIt("vec3 v1 =", "float3 v1 ="));                           // other language's type
        CHECK(breakIt("0.8, 0.8, 0.8)", "0.8.1, 0.8, 0.8)"));                 // malformed number
        CHECK(breakIt("0.8, 0.8, 0.8)", "0.8, 0.8, 0.8x)"));
        CHECK(breakIt("okm_v3(0.8", "okm_v9(0.8"));                           // macro that does not exist
        CHECK(breakIt("    float v2 = 0.0;", "    float v2 = 0.0 @;"));        // stray character
        CHECK(breakIt("mix(a,b,t)", "lerp(a,b,t)"));                          // wrong-language function
        CHECK(breakIt("#define okm_add(a,b) ((a)+(b))", "#define okm_add(a,b) ((a)+(b)"));
        CHECK(breakIt("// okaymat", "/* okaymat"));                           // comment never closed
        // use before declaration
        CHECK(!CheckShaderText("float f(float a) {\n    float x = y;\n    float y = 1.0;\n    return x;\n}\n", ShaderLang::GLSL).empty());
        CHECK(CheckShaderText("float f(float a) {\n    float y = 1.0;\n    float x = y + a;\n    return x;\n}\n", ShaderLang::GLSL).empty());
        // a variable declared twice
        CHECK(!CheckShaderText("void f() {\n    float v1 = 1.0;\n    float v1 = 2.0;\n}\n", ShaderLang::GLSL).empty());
        // macro used before its #define
        CHECK(!CheckShaderText("float f() {\n    return M(1.0);\n}\n#define M(x) (x)\n", ShaderLang::GLSL).empty());
        CHECK(CheckShaderText("#define M(x) (x)\nfloat f() {\n    return M(1.0);\n}\n", ShaderLang::GLSL).empty());
        // HLSL leakage the other way
        CHECK(!CheckShaderText("float3 f() {\n    return lerp(a, b, c);\n}\n", ShaderLang::GLSL).empty());
        CHECK(!CheckShaderText("vec3 f(vec3 a) {\n    return a;\n}\n", ShaderLang::HLSL).empty());
        CHECK(CheckShaderText("", ShaderLang::GLSL).empty());
    }

    // ---- differential: run the generated text, compare with the evaluator on random graphs ----
    for (ShaderLang lang : {ShaderLang::GLSL, ShaderLang::HLSL}) {
        std::mt19937 rng(lang == ShaderLang::GLSL ? 777 : 778);
        int graphs = 0, compared = 0, skipped = 0, valueChecks = 0, textFails = 0;
        std::set<Op> opsSeen;
        for (int iter = 0; iter < 4000; ++iter) {
            MaterialGraph g = RandomGraph(rng);
            Program p; std::vector<Diagnostic> d;
            if (!g.Compile(&p, &d)) { continue; }
            ++graphs;
            for (const IrOp& o : p.ops) opsSeen.insert(o.op);
            EmittedShader e = EmitFragment(p, lang);
            if (!CheckShaderText(e.text, lang).empty()) { ++textFails; continue; }
            for (int s = 0; s < 3; ++s) {
                EvalInput in;
                auto r = [&](float lo, float hi) { return lo + (hi - lo) * ((rng() & 0xFFFF) / 65535.0f); };
                in.uv[0] = r(-1, 3); in.uv[1] = r(-1, 3);
                for (int k = 0; k < 3; ++k) { in.worldPos[k] = r(-5, 5); in.normal[k] = r(-1, 1); in.view[k] = r(-1, 1); }
                in.time = r(0, 10);
                for (const IrOp& o : p.ops) if (o.op == Op::Param) { std::array<float, 4> a{r(-1, 2), r(-1, 2), r(-1, 2), r(-1, 2)}; if (rng() & 1) in.params[o.s] = a; }
                in.texture = [](const std::string& path, int mode, float u, float v, float rgba[4]) {
                    for (int k = 0; k < 4; ++k) { float x = u * 0.7f + v * (0.3f + 0.1f * k) + (float)path.size() * 0.05f + (float)mode * 0.11f; rgba[k] = x - std::floor(x); }
                };
                MaterialOutputs want = EvaluateProgram(p, in), got{};
                Interp ip;
                bool ran = ip.Run(e, in, got);
                if (!ran) { std::cerr << "  interpreter: " << ip.err << "\n"; CHECK(ran); break; }
                float a[12] = {want.baseColor[0], want.baseColor[1], want.baseColor[2], want.metallic, want.roughness, want.emissive[0], want.emissive[1], want.emissive[2], want.opacity, want.normal[0], want.normal[1], want.normal[2]};
                float b[12] = {got.baseColor[0], got.baseColor[1], got.baseColor[2], got.metallic, got.roughness, got.emissive[0], got.emissive[1], got.emissive[2], got.opacity, got.normal[0], got.normal[1], got.normal[2]};
                bool finite = true; for (int k = 0; k < 12; ++k) finite &= std::isfinite(a[k]) && std::isfinite(b[k]);
                if (!finite) { ++skipped; continue; }
                ++compared;
                for (int k = 0; k < 12; ++k) { ++valueChecks; if (!Close(a[k], b[k])) { CHECK(Close(a[k], b[k])); std::cerr << "  value " << k << ": evaluator " << a[k] << " text " << b[k] << " (graph " << iter << ")\n"; } }
            }
        }
        CHECK(graphs > 3000);                 // most random graphs are valid, so the test means something
        CHECK(textFails == 0);
        CHECK(compared > 8000);
        CHECK(skipped < compared / 20);
        CHECK(opsSeen.size() >= 24);          // the random graphs use (nearly) every op
        std::cout << "  " << (lang == ShaderLang::GLSL ? "GLSL" : "HLSL") << ": " << graphs << " graphs, " << compared << " evaluations, "
                  << valueChecks << " values compared, " << skipped << " skipped (non-finite), " << opsSeen.size() << " op kinds\n";
    }

    TEST_MAIN_RESULT();
}
