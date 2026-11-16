#include "test_framework.hpp"
#include "okay/Render/MaterialGraph.hpp"
#include "okay/Render/ProcTexture.hpp"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>

using namespace okay;
using namespace okay::matgraph;

namespace {
bool HasMsg(const std::vector<Issue>& v, const std::string& part, bool error = true) {
    for (const Issue& i : v) if (i.error == error && i.message.find(part) != std::string::npos) return true;
    return false;
}
// Graph whose Output has one wire: `from`.`pin` into `outPin`.
Result Eval(Graph& g, const EvalContext& ctx = EvalContext()) {
    Result r; std::string err;
    bool ok = Evaluate(g, ctx, r, &err);
    if (!ok) std::cout << "  evaluate failed: " << err << "\n";
    CHECK(ok);
    return r;
}
// Golden graph: texture * tint, plus a rim and noise-driven roughness.
Graph GoldenGraph() {
    Graph g;
    int tex = g.Add("TextureSample"); g.Find(tex)->path = "tex/brick.png";
    int tint = g.AddValue("Vec3", 1.0f, 0.5f, 0.25f);
    int mul = g.Add("Multiply");
    int fr = g.Add("Fresnel"); g.SetDefault(fr, "Power", 3);
    int noise = g.Add("Noise");
    int out = g.Add("Output");
    g.Connect(tex, "RGB", mul, "A"); g.Connect(tint, "Value", mul, "B");
    g.Connect(mul, "Result", out, "BaseColor");
    g.Connect(fr, "Result", out, "Emission");   // Float into a Vec3 pin: widened
    g.Connect(noise, "Result", out, "Roughness");
    return g;
}
}  // namespace

int main() {
    RUN_SUITE("matgraph");

    // ---- library
    {
        const auto& lib = NodeLibrary();
        CHECK(lib.size() >= 25);
        std::set<std::string> names;
        for (const NodeDef& d : lib) { CHECK(names.insert(d.type).second); CHECK(FindNodeDef(d.type) == &d); }
        CHECK(FindNodeDef("Nope") == nullptr);
        CHECK(FindNodeDef("Output") != nullptr);
    }

    // ---- hand-computed values
    {
        Graph g; int out = g.Add("Output"); int f = g.AddValue("Float", 0.25f);
        g.Connect(f, "Value", out, "BaseColor");   // a Float widens to every colour channel
        g.Connect(f, "Value", out, "Roughness");
        Result r = Eval(g);
        CHECK_NEAR(r.baseColor.x, 0.25f, 1e-6f); CHECK_NEAR(r.baseColor.z, 0.25f, 1e-6f); CHECK_NEAR(r.roughness, 0.25f, 1e-6f);
        CHECK_NEAR(r.alpha, 1.0f, 1e-6f); CHECK_NEAR(r.normal.z, 1.0f, 1e-6f);   // pin defaults
    }
    {   // Lerp((1,0,0), (0,1,0), 0.25) = (0.75, 0.25, 0)
        Graph g; int out = g.Add("Output");
        int a = g.AddValue("Vec3", 1, 0, 0), b = g.AddValue("Vec3", 0, 1, 0), t = g.AddValue("Float", 0.25f), l = g.Add("Lerp");
        g.Connect(a, "Value", l, "A"); g.Connect(b, "Value", l, "B"); g.Connect(t, "Value", l, "T"); g.Connect(l, "Result", out, "BaseColor");
        Result r = Eval(g);
        CHECK_NEAR(r.baseColor.x, 0.75f, 1e-6f); CHECK_NEAR(r.baseColor.y, 0.25f, 1e-6f); CHECK_NEAR(r.baseColor.z, 0.0f, 1e-6f);
    }
    {   // Dot((1,2,3),(4,5,6)) = 32 ; Pow(2,3) = 8 ; Normalize(3,0,4) = (0.6,0,0.8)
        Graph g; int out = g.Add("Output");
        int a = g.AddValue("Vec3", 1, 2, 3), b = g.AddValue("Vec3", 4, 5, 6), d = g.Add("Dot");
        g.Connect(a, "Value", d, "A"); g.Connect(b, "Value", d, "B"); g.Connect(d, "Result", out, "Roughness");
        int v = g.AddValue("Vec3", 3, 0, 4), n = g.Add("Normalize"); g.Connect(v, "Value", n, "In"); g.Connect(n, "Result", out, "BaseColor");
        Result r = Eval(g);
        CHECK_NEAR(r.roughness, 32.0f, 1e-5f);
        CHECK_NEAR(r.baseColor.x, 0.6f, 1e-6f); CHECK_NEAR(r.baseColor.y, 0.0f, 1e-6f); CHECK_NEAR(r.baseColor.z, 0.8f, 1e-6f);
        Graph h; int o2 = h.Add("Output"); int base = h.AddValue("Float", 2), ex = h.AddValue("Float", 3), p = h.Add("Pow");
        h.Connect(base, "Value", p, "Base"); h.Connect(ex, "Value", p, "Exp"); h.Connect(p, "Result", o2, "Metallic");
        CHECK_NEAR(Eval(h).metallic, 8.0f, 1e-5f);
    }
    {   // Saturate(1.7) = 1 ; Clamp(5, 0, 3) = 3 ; OneMinus(0.3) = 0.7 ; Contrast(0.75, 2) = 1 ; Add/Subtract/Multiply/Divide
        auto one = [](const std::string& type, float a, float b, float c, const char* p1, const char* p2, const char* p3) {
            Graph g; int out = g.Add("Output"); int n = g.Add(type);
            g.SetDefault(n, p1, a); if (p2) g.SetDefault(n, p2, b); if (p3) g.SetDefault(n, p3, c);
            g.Connect(n, "Result", out, "Roughness");
            Result r; std::string e; bool ok = Evaluate(g, EvalContext(), r, &e); CHECK(ok); return r.roughness;
        };
        CHECK_NEAR(one("Saturate", 1.7f, 0, 0, "In", nullptr, nullptr), 1.0f, 1e-6f);
        CHECK_NEAR(one("Saturate", -2.0f, 0, 0, "In", nullptr, nullptr), 0.0f, 1e-6f);
        CHECK_NEAR(one("Clamp", 5, 0, 3, "Value", "Min", "Max"), 3.0f, 1e-6f);
        CHECK_NEAR(one("OneMinus", 0.3f, 0, 0, "In", nullptr, nullptr), 0.7f, 1e-6f);
        CHECK_NEAR(one("Contrast", 0.75f, 2, 0, "In", "Amount", nullptr), 1.0f, 1e-6f);
        CHECK_NEAR(one("Add", 1.5f, 2.0f, 0, "A", "B", nullptr), 3.5f, 1e-6f);
        CHECK_NEAR(one("Subtract", 1.5f, 2.0f, 0, "A", "B", nullptr), -0.5f, 1e-6f);
        CHECK_NEAR(one("Multiply", 1.5f, 2.0f, 0, "A", "B", nullptr), 3.0f, 1e-6f);
        CHECK_NEAR(one("Divide", 3.0f, 2.0f, 0, "A", "B", nullptr), 1.5f, 1e-6f);
        CHECK_NEAR(one("Divide", 3.0f, 0.0f, 0, "A", "B", nullptr), 0.0f, 1e-6f);   // CPU rule: divide by zero is 0
        CHECK_NEAR(one("Pow", -4.0f, 2.0f, 0, "Base", "Exp", nullptr), 0.0f, 1e-6f); // negative base counts as 0
    }
    {   // Sin(Time), UV split, Fresnel
        Graph g; int out = g.Add("Output");
        int t = g.Add("Time"), s = g.Add("Sin"); g.Connect(t, "Time", s, "In"); g.Connect(s, "Result", out, "Metallic");
        int uv = g.Add("UV"), sp = g.Add("Split"); g.Connect(uv, "UV", sp, "In"); g.Connect(sp, "Y", out, "Roughness");
        int fr = g.Add("Fresnel"); g.SetDefault(fr, "Power", 2); g.Connect(fr, "Result", out, "Alpha");
        EvalContext c; c.time = 1.5707963f; c.uv = {0.3f, 0.7f};
        c.normal = {0, 0, 1}; c.viewDir = Vec3(0.6f, 0, 0.8f);   // N.V = 0.8 -> (1-0.8)^2 = 0.04
        Result r = Eval(g, c);
        CHECK_NEAR(r.metallic, 1.0f, 1e-6f); CHECK_NEAR(r.roughness, 0.7f, 1e-6f); CHECK_NEAR(r.alpha, 0.04f, 1e-5f);
        c.viewDir = {0, 0, 1};
        CHECK_NEAR(Eval(g, c).alpha, 0.0f, 1e-6f);   // facing the camera: no rim
    }
    {   // Desaturate((1,0,0), 1) = 0.2126 in each channel ; (…, 0) = unchanged ; MakeVec3
        Graph g; int out = g.Add("Output");
        int c = g.AddValue("Vec3", 1, 0, 0), d = g.Add("Desaturate"); g.Connect(c, "Value", d, "Colour"); g.Connect(d, "Result", out, "BaseColor");
        Result r = Eval(g);
        CHECK_NEAR(r.baseColor.x, 0.2126f, 1e-6f); CHECK_NEAR(r.baseColor.y, 0.2126f, 1e-6f);
        g.SetDefault(d, "Amount", 0);
        r = Eval(g); CHECK_NEAR(r.baseColor.x, 1.0f, 1e-6f); CHECK_NEAR(r.baseColor.y, 0.0f, 1e-6f);
        Graph h; int o2 = h.Add("Output"); int m = h.Add("MakeVec3"); h.SetDefault(m, "X", 0.1f); h.SetDefault(m, "Y", 0.2f); h.SetDefault(m, "Z", 0.3f);
        h.Connect(m, "Result", o2, "Emission");
        r = Eval(h); CHECK_NEAR(r.emission.x, 0.1f, 1e-6f); CHECK_NEAR(r.emission.z, 0.3f, 1e-6f);
    }
    {   // Noise: equals the lattice hash on a lattice point, stays in 0..1, is smooth
        Graph g; int out = g.Add("Output"); int n = g.Add("Noise"); g.SetDefault(n, "Scale", 1);
        g.Connect(n, "Result", out, "Roughness");
        float lo = 1, hi = 0, maxStep = 0, prev = -1;
        for (int i = 0; i < 400; ++i) {
            EvalContext c; c.uv = {0.0137f * i, 0.0091f * i};
            float v = Eval(g, c).roughness;
            lo = std::min(lo, v); hi = std::max(hi, v);
            if (prev >= 0) maxStep = std::max(maxStep, std::fabs(v - prev));
            prev = v;
        }
        CHECK(lo >= 0.0f && hi <= 1.0f); CHECK(hi - lo > 0.2f); CHECK(maxStep < 0.1f);
        EvalContext c; c.uv = {2.0f, 3.0f};
        float s = std::sin(2.0f * 91.3458f + 3.0f * 47.8231f) * 28461.1357f;
        CHECK_NEAR(Eval(g, c).roughness, s - std::floor(s), 1e-6f);
        EvalContext c2 = c; CHECK_NEAR(Eval(g, c2).roughness, Eval(g, c).roughness, 0.0f);   // deterministic
    }
    {   // Texture sample and normal map through the sampler callback
        Graph g; int out = g.Add("Output"); int tex = g.Add("TextureSample"); g.Find(tex)->path = "a.png";
        g.Connect(tex, "RGB", out, "BaseColor"); g.Connect(tex, "A", out, "Alpha");
        int nm = g.Add("NormalMap"); g.SetDefault(nm, "Strength", 0.5f); g.Connect(nm, "Normal", out, "Normal");
        std::string seen; float su = -1, sv = -1;
        EvalContext c; c.uv = {0.25f, 0.5f};
        Result r = Eval(g, c);   // no sampler: white and a flat normal
        CHECK_NEAR(r.baseColor.x, 1.0f, 1e-6f); CHECK_NEAR(r.normal.z, 1.0f, 1e-6f); CHECK_NEAR(r.normal.x, 0.0f, 1e-6f);
        c.sampler = [&](const std::string& p, float u, float v, float* rgba) {
            seen = p; su = u; sv = v; rgba[0] = 1.0f; rgba[1] = 0.5f; rgba[2] = 1.0f; rgba[3] = 0.4f;
        };
        r = Eval(g, c);
        CHECK(seen == "a.png" || seen.empty() == false); CHECK_NEAR(su, 0.25f, 1e-6f); CHECK_NEAR(sv, 0.5f, 1e-6f);
        CHECK_NEAR(r.alpha, 0.4f, 1e-6f);
        // (1, 0.5, 1) -> (1, 0, 1) -> xy * 0.5 -> (0.5, 0, 1) -> normalized
        CHECK_NEAR(r.normal.x, 0.4472136f, 1e-5f); CHECK_NEAR(r.normal.y, 0.0f, 1e-5f); CHECK_NEAR(r.normal.z, 0.8944272f, 1e-5f);
    }

    // ---- text round trip and files
    {
        Graph g = ExampleGraph();
        std::string t = g.ToText();
        CHECK(t.rfind("okaymatgraph 1\n", 0) == 0);
        Graph h; std::string err;
        CHECK(Graph::FromText(t, h, &err));
        CHECK(h.ToText() == t);
        CHECK(h.nodes.size() == g.nodes.size() && h.links.size() == g.links.size());
        EvalContext c; c.uv = {0.37f, 0.61f}; c.normal = Vec3(0.3f, 0.2f, 0.93f).Normalized(); c.viewDir = Vec3(0.1f, 0.1f, 0.99f).Normalized(); c.time = 1.0f;
        Result a = Eval(g, c), b = Eval(h, c);
        CHECK(a.baseColor == b.baseColor); CHECK(a.emission == b.emission); CHECK_NEAR(a.roughness, b.roughness, 0.0f);
        CHECK(!HasErrors(Validate(g)));

        Graph tp; int tex = tp.Add("TextureSample"); tp.Find(tex)->path = "my folder/brick wall.png"; tp.Find(tex)->posX = -12.5f;
        tp.Add("Output");
        Graph tq; CHECK(Graph::FromText(tp.ToText(), tq, &err));
        CHECK(tq.nodes[0].path == "my folder/brick wall.png"); CHECK_NEAR(tq.nodes[0].posX, -12.5f, 0.0f);

        std::string path = "matgraph_roundtrip.okaymatgraph";
        CHECK(g.SaveToFile(path));
        Graph f; CHECK(Graph::LoadFromFile(path, f, &err)); CHECK(f.ToText() == t);
        std::remove(path.c_str());
        Graph nf; CHECK(!Graph::LoadFromFile("does/not/exist.okaymatgraph", nf, &err)); CHECK(!err.empty());
    }

    // ---- malformed files: each is refused with a line number, none crash
    {
        auto bad = [](const std::string& text, const std::string& part) {
            Graph g; std::string err;
            bool ok = Graph::FromText(text, g, &err);
            if (ok || err.find(part) == std::string::npos) std::cout << "  unexpected for [" << text << "]: ok=" << ok << " err=" << err << "\n";
            CHECK(!ok); CHECK(err.find(part) != std::string::npos);
        };
        bad("", "empty");
        bad("hello\n", "not a material graph");
        bad("okaymatgraph\n", "not a material graph");
        bad("okaymatgraph 2\n", "version 2");
        bad("okaymatgraph x\n", "bad version");
        bad("okaymatgraph 1\nnode\n", "line 2");
        bad("okaymatgraph 1\nnode x Float\n", "bad node id");
        bad("okaymatgraph 1\nnode 1 Float v=1,2,3\n", "bad v=");
        bad("okaymatgraph 1\nnode 1 Float v=nan,0,0,0\n", "bad v=");
        bad("okaymatgraph 1\nnode 1 Float v=inf,0,0,0\n", "bad v=");
        bad("okaymatgraph 1\nnode 1 Float v=1,2,3,4,5\n", "bad v=");
        bad("okaymatgraph 1\nnode 1 Float pos=1\n", "bad pos=");
        bad("okaymatgraph 1\nnode 1 Float color=red\n", "unknown node field");
        bad("okaymatgraph 1\nlink 1.Value\n", "link needs");
        bad("okaymatgraph 1\nlink a.Value 2.A\n", "link needs");
        bad("okaymatgraph 1\nlink 1. 2.A\n", "link needs");
        bad("okaymatgraph 1\ndef 9.A 1,2,3,4\n", "not (yet) defined");
        bad("okaymatgraph 1\nfrobnicate 1\n", "unknown line");
        bad("okaymatgraph 1\nnode 99999999999 Float\n", "bad node id");
        bad(std::string("okaymatgraph 1\n") + std::string(2 << 20, '#'), "too big");
        std::string many = "okaymatgraph 1\n";
        for (int i = 1; i <= kMaxNodes + 1; ++i) many += "node " + std::to_string(i) + " Float\n";
        bad(many, "too many nodes");
        std::string manyL = "okaymatgraph 1\nnode 1 Float\n";
        for (int i = 0; i <= kMaxLinks; ++i) manyL += "link 1.Value 1.Value\n";
        bad(manyL, "too many links");
        // comments, blank lines, CRLF and Windows line ends are fine
        Graph g; std::string err;
        CHECK(Graph::FromText("# comment\r\n\r\nokaymatgraph 1\r\nnode 1 Output\r\n", g, &err));
        CHECK(g.nodes.size() == 1);
        // random garbage never crashes
        unsigned seed = 12345;
        for (int i = 0; i < 300; ++i) {
            std::string junk = "okaymatgraph 1\n";
            for (int k = 0; k < 80; ++k) { seed = seed * 1664525u + 1013904223u; junk += "nodelinkdef .=,1 \n\tFloat9"[(seed >> 16) % 25]; }
            Graph tmp; std::string e; if (Graph::FromText(junk, tmp, &e)) Validate(tmp);
        }
    }

    // ---- validation
    {
        Graph g; g.nodes.push_back(Node()); g.nodes[0].id = 1; g.nodes[0].type = "Bogus";
        auto v = Validate(g);
        CHECK(HasMsg(v, "unknown node type 'Bogus'")); CHECK(HasMsg(v, "no Output"));
        Graph none; CHECK(HasMsg(Validate(none), "no Output node"));
        Graph two; two.Add("Output"); two.Add("Output"); CHECK(HasMsg(Validate(two), "2 Output nodes"));
        Graph dup; dup.Add("Output"); Node n; n.id = 1; n.type = "Float"; dup.nodes.push_back(n); CHECK(HasMsg(Validate(dup), "share id 1"));
        // cycle: Add -> Multiply -> Add
        Graph cyc; int o = cyc.Add("Output"), a = cyc.Add("Add"), m = cyc.Add("Multiply");
        cyc.Connect(a, "Result", m, "A"); cyc.Connect(m, "Result", a, "A"); cyc.Connect(m, "Result", o, "Roughness");
        CHECK(HasMsg(Validate(cyc), "cycle"));
        Program p; CHECK(!Compile(cyc, p)); CHECK(!p.valid());
        Result r; std::string err; CHECK(!Evaluate(cyc, EvalContext(), r, &err)); CHECK(err.find("cycle") != std::string::npos);
        Graph self; int so = self.Add("Output"), sa = self.Add("Add"); self.Connect(sa, "Result", sa, "A"); self.Connect(sa, "Result", so, "Roughness");
        CHECK(HasMsg(Validate(self), "cycle"));
        // type mismatch: a Vec3 cannot go into the Float pin Roughness; a Vec2 cannot go into a Vec3 pin
        Graph ty; int to = ty.Add("Output"); int c3 = ty.AddValue("Vec3", 1, 1, 1), c2 = ty.AddValue("Vec2", 1, 1);
        ty.Connect(c3, "Value", to, "Roughness"); ty.Connect(c2, "Value", to, "BaseColor");
        auto tv = Validate(ty);
        CHECK(HasMsg(tv, "cannot wire a Vec3 into input 'Roughness'")); CHECK(HasMsg(tv, "cannot wire a Vec2 into input 'BaseColor'"));
        // allowed: Float into Vec3, Vec4 into Vec3
        Graph okg; int oo = okg.Add("Output"); int tx = okg.Add("TextureSample"), fl = okg.AddValue("Float", 1);
        okg.Connect(tx, "RGBA", oo, "BaseColor"); okg.Connect(fl, "Value", oo, "Emission");
        CHECK(!HasErrors(Validate(okg)));
        // unknown pins, missing nodes, two wires into one input
        Graph pn; int po = pn.Add("Output"), pf = pn.AddValue("Float", 1);
        pn.links.push_back({pf, "Nope", po, "Roughness"}); pn.links.push_back({pf, "Value", po, "Nope"}); pn.links.push_back({77, "Value", po, "Alpha"});
        auto pv = Validate(pn);
        CHECK(HasMsg(pv, "no output pin 'Nope'")); CHECK(HasMsg(pv, "no input pin 'Nope'")); CHECK(HasMsg(pv, "node 77 which does not exist"));
        Graph tw; int wo = tw.Add("Output"), w1 = tw.AddValue("Float", 1), w2 = tw.AddValue("Float", 2);
        tw.links.push_back({w1, "Value", wo, "Roughness"}); tw.links.push_back({w2, "Value", wo, "Roughness"});
        CHECK(HasMsg(Validate(tw), "two wires"));
        // unused node is a warning only
        Graph un; un.Add("Output"); un.Add("Time");
        auto uv = Validate(un);
        CHECK(!HasErrors(uv)); CHECK(HasMsg(uv, "does not reach the Output", false));
        // unknown node loaded from a file: loads, then fails the check
        Graph lf; std::string e2; CHECK(Graph::FromText("okaymatgraph 1\nnode 1 Output\nnode 2 Wobble\nlink 2.Out 1.Roughness\n", lf, &e2));
        CHECK(HasMsg(Validate(lf), "unknown node type 'Wobble'"));
    }

    // ---- shader text (golden strings; hand-checked against the node rules in docs/material_graph.md)
    {
        Graph g = GoldenGraph();
        std::string err;
        std::string glsl = EmitShader(g, Target::Glsl120, &err);
        std::string hlsl = EmitShader(g, Target::Hlsl, &err);
        const char* wantGlsl = R"(// okaymatgraph -> GLSL 120
uniform sampler2D okay_tex_1;   // tex/brick.png
float okay_hash(vec2 c) { return fract(sin(dot(c, vec2(91.3458, 47.8231))) * 28461.1357); }
float okay_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = okay_hash(i);
    float b = okay_hash(i + vec2(1.0, 0.0));
    float c = okay_hash(i + vec2(0.0, 1.0));
    float d = okay_hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
void okay_material(vec2 uv, vec3 n_surface, vec3 view, float time,
                   out vec3 o_base_color, out vec3 o_emission, out float o_roughness,
                   out float o_metallic, out float o_alpha, out vec3 o_normal)
{
    float n5_result = okay_noise(uv * 8.0);
    float n4_result = pow(1.0 - clamp(dot(n_surface, view), 0.0, 1.0), 3.0);
    vec3 n2_value = vec3(1.0, 0.5, 0.25);
    vec4 n1_rgba = texture2D(okay_tex_1, uv);
    vec3 n1_rgb = n1_rgba.xyz;
    float n1_a = n1_rgba.w;
    vec3 n3_result = (n1_rgb * n2_value);
    o_base_color = n3_result;
    o_emission = vec3(n4_result);
    o_roughness = n5_result;
    o_metallic = 0.0;
    o_alpha = 1.0;
    o_normal = vec3(0.0, 0.0, 1.0);
}
)";
        const char* wantHlsl = R"(// okaymatgraph -> HLSL
Texture2D okay_tex_1;   // tex/brick.png
SamplerState okay_samp;
float okay_hash(float2 c) { return frac(sin(dot(c, float2(91.3458, 47.8231))) * 28461.1357); }
float okay_noise(float2 p) {
    float2 i = floor(p);
    float2 f = p - i;
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = okay_hash(i);
    float b = okay_hash(i + float2(1.0, 0.0));
    float c = okay_hash(i + float2(0.0, 1.0));
    float d = okay_hash(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
void okay_material(float2 uv, float3 n_surface, float3 view, float time,
                   out float3 o_base_color, out float3 o_emission, out float o_roughness,
                   out float o_metallic, out float o_alpha, out float3 o_normal)
{
    float n5_result = okay_noise(uv * 8.0);
    float n4_result = pow(1.0 - saturate(dot(n_surface, view)), 3.0);
    float3 n2_value = float3(1.0, 0.5, 0.25);
    float4 n1_rgba = okay_tex_1.Sample(okay_samp, uv);
    float3 n1_rgb = n1_rgba.xyz;
    float n1_a = n1_rgba.w;
    float3 n3_result = (n1_rgb * n2_value);
    o_base_color = n3_result;
    o_emission = float3(n4_result, n4_result, n4_result);
    o_roughness = n5_result;
    o_metallic = 0.0;
    o_alpha = 1.0;
    o_normal = float3(0.0, 0.0, 1.0);
}
)";
        if (glsl != wantGlsl) std::cout << "---- GLSL got:\n" << glsl;
        if (hlsl != wantHlsl) std::cout << "---- HLSL got:\n" << hlsl;
        CHECK(glsl == wantGlsl); CHECK(hlsl == wantHlsl);
        Graph bad; CHECK(EmitShader(bad, Target::Glsl120, &err).empty()); CHECK(!err.empty());
        // emitting never uses an int literal where a float is needed, and is stable
        CHECK(EmitShader(g, Target::Glsl120) == glsl);
    }

    // ---- the shipped examples load, check clean, and match what the docs say
    {
#ifdef OKAY_EXAMPLES_DIR
        for (const char* f : {"rim_and_noise", "checker_metal"}) {
            Graph g; std::string err;
            std::string path = std::string(OKAY_EXAMPLES_DIR) + "/material_graph/" + f + ".okaymatgraph";
            bool ok = Graph::LoadFromFile(path, g, &err);
            if (!ok) std::cout << "  " << path << ": " << err << "\n";
            CHECK(ok);
            if (!ok) continue;
            auto issues = Validate(g);
            CHECK(!HasErrors(issues)); CHECK(issues.empty());
            CHECK(!EmitShader(g, Target::Glsl120).empty()); CHECK(!EmitShader(g, Target::Hlsl).empty());
        }
        {   // rim_and_noise is exactly ExampleGraph(), so the editor's built-in example and the file agree
            Graph g; Graph::LoadFromFile(std::string(OKAY_EXAMPLES_DIR) + "/material_graph/rim_and_noise.okaymatgraph", g);
            CHECK(g.ToText() == ExampleGraph().ToText());
        }
#endif
    }

    // ---- preview sphere
    {
        Graph g = ExampleGraph(); Program p; CHECK(Compile(g, p));
        Image img = RenderPreviewSphere(p, 64, 0.0f);
        Image big = RenderPreviewSphere(p, 256, 1.0f);
        CHECK(img.Valid() && img.Width() == 64);
        CHECK(img.GetPixel(0, 0).a == 0.0f);          // corner: outside the sphere
        CHECK(img.GetPixel(32, 32).a > 0.99f);        // centre: on the sphere
        Graph red; int o = red.Add("Output"); int c = red.AddValue("Vec3", 1, 0, 0); red.Connect(c, "Value", o, "BaseColor");
        Program pr; CHECK(Compile(red, pr));
        Image ri = RenderPreviewSphere(pr, 32);
        Color mid = ri.GetPixel(16, 16);
        CHECK(mid.r > mid.g * 3 && mid.r > mid.b * 3);   // a red material is red
        const char* shot = std::getenv("OKAY_MATGRAPH_SHOT_DIR");
        if (shot) {
            big.SavePNG(std::string(shot) + "/matgraph_rim_and_noise.png");
            Graph cm; Graph::LoadFromFile(std::string(OKAY_EXAMPLES_DIR) + "/material_graph/checker_metal.okaymatgraph", cm);
            Program pc; Compile(cm, pc);
            Image chk = GenerateProcTexture("checker");   // the same texture the editor preview reads for proc:checker
            auto smp = [&](const std::string&, float u, float v, float* o) {
                Color c = chk.Sample(u - std::floor(u), v - std::floor(v)); o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = c.a; };
            RenderPreviewSphere(pc, 256, 0.0f, smp).SavePNG(std::string(shot) + "/matgraph_checker_metal.png");
        }
    }

    TEST_MAIN_RESULT();
}
