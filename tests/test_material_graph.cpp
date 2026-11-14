// Material graph phase 1: model, checker, .okaymat graph text, CPU evaluator, hostile input.
// Codegen and the differential test are in test_material_graph_codegen.cpp.
#include "test_framework.hpp"
#include "okay/Render/MaterialGraph.hpp"
#include "okay/Render/Material.hpp"
#include <cmath>
#include <random>

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

bool HasCode(const std::vector<Diagnostic>& d, const char* code) {
    for (const Diagnostic& x : d) if (x.severity == Severity::Error && x.code == code) return true;
    return false;
}
int Errors(const std::vector<Diagnostic>& d) { int n = 0; for (auto& x : d) n += x.severity == Severity::Error; return n; }

// Result of Check on g: true when there are no errors. Fills the codes.
bool Ok(const MaterialGraph& g) { std::vector<Diagnostic> d; return g.Check(&d); }

MaterialOutputs Eval(const MaterialGraph& g, const EvalInput& in = EvalInput()) {
    Program p; std::vector<Diagnostic> d;
    bool ok = g.Compile(&p, &d);
    CHECK(ok);
    return EvaluateProgram(p, in);
}

MaterialGraph Sample() {   // a graph that uses most things
    MaterialGraph g = MaterialGraph::MakeDefault();       // id 1 = Output
    int tex = g.AddNode(NodeKind::TextureObject, 10, 20); g.Find(tex)->text = "assets/my brick%.png";
    int smp = g.AddNode(NodeKind::SamplerState); g.Find(smp)->v[0] = 1;
    int ts = g.AddNode(NodeKind::TextureSample, 100, 5);
    Wire(g, tex, "Texture", ts, "Texture"); Wire(g, smp, "Sampler", ts, "Sampler");
    int pan = g.AddNode(NodeKind::Panner); g.Find(pan)->v[0] = 0.25f;
    int sp = g.AddParam(NodeKind::ParamVec2, "scroll", 0.1f, -0.2f);
    Wire(g, sp, "Value", pan, "Speed"); Wire(g, pan, "UV", ts, "UV");
    int tint = g.AddParam(NodeKind::ParamVec3, "tint", 1, 0.5f, 0.25f);
    int mul = g.AddNode(NodeKind::Multiply);
    Wire(g, ts, "RGB", mul, "A"); Wire(g, tint, "Value", mul, "B");
    Wire(g, mul, "Result", 1, "BaseColor");
    int fr = g.AddNode(NodeKind::Fresnel);
    int sat = g.AddNode(NodeKind::Saturate);
    Wire(g, fr, "Result", sat, "X"); Wire(g, sat, "Result", 1, "Roughness");
    int nz = g.AddNode(NodeKind::Noise);
    Wire(g, nz, "Value", 1, "Metallic");
    int em = g.AddNode(NodeKind::ConstVec3); g.Find(em)->v[0] = 0.1f; g.Find(em)->v[2] = -3e-20f;
    Wire(g, em, "Value", 1, "Emissive");
    return g;
}

} // namespace

int main() {
    RUN_SUITE("material_graph");

    // ---- the node table ----
    {
        int n = (int)NodeKind::Count;
        CHECK(n == 29);
        for (int i = 0; i < n; ++i) {
            const NodeDef& d = GetNodeDef((NodeKind)i);
            CHECK((int)d.kind == i);
            NodeKind back; CHECK(NodeKindFromName(d.name, back) && back == d.kind);
            for (int a = 0; a < i; ++a) CHECK(std::string(GetNodeDef((NodeKind)a).name) != d.name);
        }
        NodeKind k; CHECK(!NodeKindFromName("Nope", k)); CHECK(!NodeKindFromName("", k));
        CHECK(GetNodeDef(NodeKind::Output).in.size() == 6);
        CHECK(CanConnect(PinType::Float, PinType::Vec3));
        CHECK(!CanConnect(PinType::Vec3, PinType::Float));
        CHECK(!CanConnect(PinType::Vec2, PinType::Vec3));
        CHECK(!CanConnect(PinType::Float, PinType::Texture));
        CHECK(CanConnect(PinType::Texture, PinType::Texture));
        CHECK(!CanConnect(PinType::Texture, PinType::Sampler));
    }

    // ---- checker: good graphs ----
    {
        MaterialGraph g = MaterialGraph::MakeDefault();
        CHECK(Ok(g));
        CHECK(Ok(Sample()));
        // a float widens into a vec3 pin, and into a generic node next to a vec3
        MaterialGraph w = MaterialGraph::MakeDefault();
        int c = w.AddConst(0.5f);
        Wire(w, c, "Value", 1, "BaseColor");
        int a = w.AddNode(NodeKind::Add);
        int v3 = w.AddConst(1, 2, 3);
        Wire(w, v3, "Value", a, "A"); Wire(w, c, "Value", a, "B");
        Wire(w, a, "Result", 1, "Emissive");
        CHECK(Ok(w));
        // unused node gives only an Info
        MaterialGraph u = MaterialGraph::MakeDefault(); u.AddNode(NodeKind::Time);
        std::vector<Diagnostic> d; CHECK(u.Check(&d));
        bool info = false; for (auto& x : d) info |= x.severity == Severity::Info && x.code == "unused";
        CHECK(info);
    }

    // ---- checker: bad graphs ----
    {
        std::vector<Diagnostic> d;
        MaterialGraph g;                                   // no output
        CHECK(!g.Check(&d)); CHECK(HasCode(d, "no-output"));

        g = MaterialGraph::MakeDefault(); g.AddNode(NodeKind::Output);
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "many-outputs"));

        g = MaterialGraph::MakeDefault();                  // required input open
        int nm = g.AddNode(NodeKind::Normalize);
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "missing-input"));
        (void)nm;

        g = MaterialGraph::MakeDefault();                  // texture sample without a texture
        int ts = g.AddNode(NodeKind::TextureSample);
        Wire(g, ts, "RGB", 1, "BaseColor");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "missing-input"));

        g = MaterialGraph::MakeDefault();                  // vec2 into vec3
        int c2 = g.AddNode(NodeKind::ConstVec2); Wire(g, c2, "Value", 1, "BaseColor");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "type"));

        g = MaterialGraph::MakeDefault();                  // vec3 into float (no narrowing)
        int c3 = g.AddNode(NodeKind::ConstVec3); Wire(g, c3, "Value", 1, "Roughness");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "type"));

        g = MaterialGraph::MakeDefault();                  // texture into a maths node
        int to = g.AddNode(NodeKind::TextureObject); g.Find(to)->text = "a.png";
        int ad = g.AddNode(NodeKind::Add);
        Wire(g, to, "Texture", ad, "A"); Wire(g, ad, "Result", 1, "Roughness");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "type"));

        g = MaterialGraph::MakeDefault();                  // sampler into the texture pin
        int sm = g.AddNode(NodeKind::SamplerState); ts = g.AddNode(NodeKind::TextureSample);
        Wire(g, sm, "Sampler", ts, "Texture");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "type"));

        g = MaterialGraph::MakeDefault();                  // vec2 + vec3
        c2 = g.AddNode(NodeKind::ConstVec2); c3 = g.AddNode(NodeKind::ConstVec3); ad = g.AddNode(NodeKind::Add);
        Wire(g, c2, "Value", ad, "A"); Wire(g, c3, "Value", ad, "B"); Wire(g, ad, "Result", 1, "BaseColor");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "type"));

        g = MaterialGraph::MakeDefault();                  // Normalize of a float
        int cf = g.AddConst(2); int nn = g.AddNode(NodeKind::Normalize);
        Wire(g, cf, "Value", nn, "X"); Wire(g, nn, "Result", 1, "Roughness");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "type"));

        g = MaterialGraph::MakeDefault();                  // cycle of two
        int a1 = g.AddNode(NodeKind::Add), a2 = g.AddNode(NodeKind::Add);
        Wire(g, a1, "Result", a2, "A"); Wire(g, a2, "Result", a1, "A"); Wire(g, a2, "Result", 1, "Roughness");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "cycle"));

        g = MaterialGraph::MakeDefault();                  // self loop
        a1 = g.AddNode(NodeKind::Add); Wire(g, a1, "Result", a1, "A");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "cycle"));

        g = MaterialGraph::MakeDefault();                  // two links into one pin
        int k1 = g.AddConst(1), k2 = g.AddConst(2);
        Wire(g, k1, "Value", 1, "Roughness"); Wire(g, k2, "Value", 1, "Roughness");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "duplicate-link"));

        g = MaterialGraph::MakeDefault();                  // links to nothing, bad pins
        g.Connect(99, 0, 1, 0);
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "bad-link"));
        g = MaterialGraph::MakeDefault(); k1 = g.AddConst(1);
        g.Connect(k1, 5, 1, 1);
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "bad-link"));
        g = MaterialGraph::MakeDefault(); k1 = g.AddConst(1);
        g.Connect(k1, 0, 1, -1);
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "bad-link"));
        g = MaterialGraph::MakeDefault(); k1 = g.AddConst(1);
        g.Connect(1, 0, k1, 0);                            // out of the Output node (it has no outputs)
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "bad-link"));

        g = MaterialGraph::MakeDefault();                  // duplicate id
        g.AddConst(1); g.nodes.back().id = 1;
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "duplicate-id"));

        g = MaterialGraph::MakeDefault();                  // parameter names
        g.AddParam(NodeKind::ParamFloat, "ok");
        g.AddParam(NodeKind::ParamFloat, "ok");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "duplicate-param"));
        for (const char* bad : {"", "1abc", "a b", "a-b", "x;", "a.b"}) {
            g = MaterialGraph::MakeDefault(); g.AddParam(NodeKind::ParamVec3, bad);
            d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "bad-param-name"));
        }
        g = MaterialGraph::MakeDefault(); g.AddParam(NodeKind::ParamFloat, std::string(65, 'a'));
        d.clear(); CHECK(!g.Check(&d));

        g = MaterialGraph::MakeDefault(); g.AddNode(NodeKind::TextureObject);   // empty path
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "bad-texture-path"));
        g = MaterialGraph::MakeDefault(); to = g.AddNode(NodeKind::TextureObject); g.Find(to)->text = "a\nb";
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "bad-texture-path"));

        for (float m : {-1.0f, 4.0f, 0.5f}) {
            g = MaterialGraph::MakeDefault(); sm = g.AddNode(NodeKind::SamplerState); g.Find(sm)->v[0] = m;
            d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "bad-sampler"));
        }
        g = MaterialGraph::MakeDefault(); k1 = g.AddConst(1);
        g.Find(k1)->v[0] = std::nanf("");
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "bad-value"));
        g.Find(k1)->v[0] = INFINITY;
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "bad-value"));

        g = MaterialGraph::MakeDefault(); g.nodes.push_back(Node()); g.nodes.back().id = 7;
        g.nodes.back().kind = (NodeKind)200;
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "bad-kind"));

        // limits
        g = MaterialGraph::MakeDefault();
        for (int i = 0; i < kMaxNodes + 1; ++i) g.AddNode(NodeKind::Time);
        d.clear(); CHECK(!g.Check(&d)); CHECK(HasCode(d, "limit"));

        // errors are reported all at once, not one at a time
        g = MaterialGraph::MakeDefault();
        g.AddNode(NodeKind::Normalize); g.AddNode(NodeKind::Normalize);
        d.clear(); g.Check(&d); CHECK(Errors(d) >= 2);
    }

    // ---- evaluator: golden values ----
    {
        MaterialOutputs m = Eval(MaterialGraph::MakeDefault());
        for (int k = 0; k < 3; ++k) CHECK_NEAR(m.baseColor[k], 0.8f, 1e-6);
        CHECK_NEAR(m.metallic, 0.0f, 1e-6); CHECK_NEAR(m.roughness, 0.5f, 1e-6); CHECK_NEAR(m.opacity, 1.0f, 1e-6);
        CHECK_NEAR(m.normal[2], 1.0f, 1e-6); CHECK_NEAR(m.emissive[0], 0.0f, 1e-6);

        auto unary = [&](NodeKind k, const char* in, float v, const char* out = "Result") {
            MaterialGraph g = MaterialGraph::MakeDefault();
            int c = g.AddConst(v), n = g.AddNode(k);
            Wire(g, c, "Value", n, in); Wire(g, n, out, 1, "Roughness");
            return Eval(g).roughness;
        };
        CHECK_NEAR(unary(NodeKind::Saturate, "X", 1.7f), 1.0, 1e-6);
        CHECK_NEAR(unary(NodeKind::Saturate, "X", -0.3f), 0.0, 1e-6);
        CHECK_NEAR(unary(NodeKind::Saturate, "X", 0.3f), 0.3, 1e-6);
        CHECK_NEAR(unary(NodeKind::OneMinus, "X", 0.25f), 0.75, 1e-6);

        auto binary = [&](NodeKind k, const char* a, float x, const char* b, float y) {
            MaterialGraph g = MaterialGraph::MakeDefault();
            int c1 = g.AddConst(x), c2 = g.AddConst(y), n = g.AddNode(k);
            Wire(g, c1, "Value", n, a); Wire(g, c2, "Value", n, b); Wire(g, n, "Result", 1, "Roughness");
            return Eval(g).roughness;
        };
        CHECK_NEAR(binary(NodeKind::Add, "A", 1.5f, "B", 2.25f), 3.75, 1e-6);
        CHECK_NEAR(binary(NodeKind::Multiply, "A", 1.5f, "B", -2.0f), -3.0, 1e-6);
        CHECK_NEAR(binary(NodeKind::Power, "Base", 2.0f, "Exp", 10.0f), 1024.0, 1e-3);
        CHECK_NEAR(binary(NodeKind::Power, "Base", -2.0f, "Exp", 2.0f), 0.0, 1e-6);   // negative base is clamped to 0

        // vec ops
        MaterialGraph g = MaterialGraph::MakeDefault();
        int v = g.AddConst(3, 0, 4), nz = g.AddNode(NodeKind::Normalize);
        Wire(g, v, "Value", nz, "X"); Wire(g, nz, "Result", 1, "BaseColor");
        MaterialOutputs o = Eval(g);
        CHECK_NEAR(o.baseColor[0], 0.6, 1e-6); CHECK_NEAR(o.baseColor[1], 0.0, 1e-6); CHECK_NEAR(o.baseColor[2], 0.8, 1e-6);
        g = MaterialGraph::MakeDefault();                      // normalize of zero is zero, not NaN
        v = g.AddConst(0, 0, 0); nz = g.AddNode(NodeKind::Normalize);
        Wire(g, v, "Value", nz, "X"); Wire(g, nz, "Result", 1, "BaseColor");
        o = Eval(g); CHECK(o.baseColor[0] == 0.0f && o.baseColor[2] == 0.0f);

        g = MaterialGraph::MakeDefault();                      // dot
        int a = g.AddConst(1, 2, 3), b = g.AddConst(4, -5, 6), dt = g.AddNode(NodeKind::Dot);
        Wire(g, a, "Value", dt, "A"); Wire(g, b, "Value", dt, "B"); Wire(g, dt, "Result", 1, "Metallic");
        CHECK_NEAR(Eval(g).metallic, 12.0, 1e-5);

        g = MaterialGraph::MakeDefault();                      // lerp with vec and float alpha
        a = g.AddConst(0, 0, 0); b = g.AddConst(10, 20, 30); int al = g.AddConst(0.25f), lp = g.AddNode(NodeKind::Lerp);
        Wire(g, a, "Value", lp, "A"); Wire(g, b, "Value", lp, "B"); Wire(g, al, "Value", lp, "Alpha");
        Wire(g, lp, "Result", 1, "Emissive");
        o = Eval(g); CHECK_NEAR(o.emissive[0], 2.5, 1e-5); CHECK_NEAR(o.emissive[1], 5.0, 1e-5); CHECK_NEAR(o.emissive[2], 7.5, 1e-5);

        g = MaterialGraph::MakeDefault();                      // clamp
        a = g.AddConst(-4, 0.5f, 9); int lo = g.AddConst(0), hi = g.AddConst(2), cl = g.AddNode(NodeKind::Clamp);
        Wire(g, a, "Value", cl, "X"); Wire(g, lo, "Value", cl, "Min"); Wire(g, hi, "Value", cl, "Max");
        Wire(g, cl, "Result", 1, "Emissive");
        o = Eval(g); CHECK_NEAR(o.emissive[0], 0, 1e-6); CHECK_NEAR(o.emissive[1], 0.5, 1e-6); CHECK_NEAR(o.emissive[2], 2, 1e-6);

        // fresnel: facing -> base reflect; edge-on -> 1
        g = MaterialGraph::MakeDefault(); int fr = g.AddNode(NodeKind::Fresnel); Wire(g, fr, "Result", 1, "Roughness");
        EvalInput in; in.normal[0] = 0; in.normal[1] = 0; in.normal[2] = 1; in.view[2] = 1;
        CHECK_NEAR(Eval(g, in).roughness, 0.04, 1e-6);
        in.view[0] = 1; in.view[2] = 0;
        CHECK_NEAR(Eval(g, in).roughness, 1.0, 1e-6);
        in.view[0] = 0.6f; in.view[2] = 0.8f;                  // 1 - 0.8 = 0.2; 0.04 + 0.96 * 0.2^5
        CHECK_NEAR(Eval(g, in).roughness, 0.04 + 0.96 * std::pow(0.2, 5.0), 1e-6);

        // panner: uv + speed * time
        g = MaterialGraph::MakeDefault(); int pn = g.AddNode(NodeKind::Panner); int sp = g.AddNode(NodeKind::ConstVec2);
        g.Find(sp)->v[0] = 2; g.Find(sp)->v[1] = -1;
        Wire(g, sp, "Value", pn, "Speed");
        int ts = g.AddNode(NodeKind::TextureSample); int tx = g.AddNode(NodeKind::TextureObject); g.Find(tx)->text = "t.png";
        Wire(g, tx, "Texture", ts, "Texture"); Wire(g, pn, "UV", ts, "UV"); Wire(g, ts, "RGB", 1, "BaseColor");
        in = EvalInput(); in.uv[0] = 0.5f; in.uv[1] = 0.25f; in.time = 3;
        float seenU = -1, seenV = -1; int seenMode = -1; std::string seenPath;
        in.texture = [&](const std::string& path, int mode, float u, float vv, float rgba[4]) {
            seenU = u; seenV = vv; seenMode = mode; seenPath = path; rgba[0] = 0.1f; rgba[1] = 0.2f; rgba[2] = 0.3f; rgba[3] = 0.4f;
        };
        o = Eval(g, in);
        CHECK_NEAR(seenU, 6.5, 1e-6); CHECK_NEAR(seenV, -2.75, 1e-6); CHECK(seenMode == 0); CHECK(seenPath == "t.png");
        CHECK_NEAR(o.baseColor[1], 0.2, 1e-6);

        // texture alpha output, and default texture (white) when no provider is set
        g = MaterialGraph::MakeDefault(); tx = g.AddNode(NodeKind::TextureObject); g.Find(tx)->text = "t.png";
        ts = g.AddNode(NodeKind::TextureSample); Wire(g, tx, "Texture", ts, "Texture"); Wire(g, ts, "A", 1, "Opacity");
        CHECK_NEAR(Eval(g).opacity, 1.0, 1e-6);
        in = EvalInput(); in.texture = [](const std::string&, int, float, float, float rgba[4]) { rgba[3] = 0.4f; };
        CHECK_NEAR(Eval(g, in).opacity, 0.4, 1e-6);

        // parameters: default and override; inputs; time
        g = MaterialGraph::MakeDefault(); int pr = g.AddParam(NodeKind::ParamFloat, "gloss", 0.7f);
        Wire(g, pr, "Value", 1, "Roughness");
        CHECK_NEAR(Eval(g).roughness, 0.7, 1e-6);
        in = EvalInput(); in.params["gloss"] = {0.1f, 0, 0, 0};
        CHECK_NEAR(Eval(g, in).roughness, 0.1, 1e-6);
        g = MaterialGraph::MakeDefault(); int tm = g.AddNode(NodeKind::Time), wp = g.AddNode(NodeKind::WorldPosition);
        Wire(g, tm, "Time", 1, "Metallic"); Wire(g, wp, "Position", 1, "Emissive");
        in = EvalInput(); in.time = 1.5f; in.worldPos[0] = 4; in.worldPos[1] = 5; in.worldPos[2] = 6;
        o = Eval(g, in); CHECK_NEAR(o.metallic, 1.5, 1e-6); CHECK_NEAR(o.emissive[2], 6, 1e-6);
        g = MaterialGraph::MakeDefault(); int vw = g.AddNode(NodeKind::ViewVector), nr = g.AddNode(NodeKind::WorldNormal);
        Wire(g, vw, "View", 1, "BaseColor"); Wire(g, nr, "Normal", 1, "Normal");
        in = EvalInput(); in.view[0] = 0.3f; in.normal[1] = 1; in.normal[2] = 0;
        o = Eval(g, in); CHECK_NEAR(o.baseColor[0], 0.3, 1e-6); CHECK_NEAR(o.normal[1], 1, 1e-6);

        // noise: range, determinism, and golden values
        CHECK_NEAR(Noise2(0, 0, 8), 0.0, 1e-7);
        float lo2 = 1, hi2 = 0;
        for (int i = 0; i < 4000; ++i) {
            float n = Noise2((float)i * 0.37f, (float)i * 0.113f, 5.0f);
            CHECK(n >= 0.0f && n <= 1.0f);
            lo2 = std::min(lo2, n); hi2 = std::max(hi2, n);
            CHECK(n == Noise2((float)i * 0.37f, (float)i * 0.113f, 5.0f));
        }
        CHECK(lo2 < 0.15f && hi2 > 0.85f);                     // it really varies
        // at integer lattice points the value is the hash itself (smoothstep(0) = 0)
        CHECK_NEAR(Noise2(3.0f, 2.0f, 1.0f), Hash12(3.0f, 2.0f), 1e-6);
        CHECK_NEAR(Hash12(1.0f, 1.0f), Hash12(1.0f, 1.0f), 0);
    }

    // ---- file text: round trip ----
    {
        MaterialGraph g = Sample();
        std::string t = g.ToText();
        CHECK(t.rfind("okaymatgraph 1\n", 0) == 0);
        MaterialGraph back;
        auto r = MaterialGraph::FromText(t, back);
        CHECK(r.ok);
        CHECK(SameGraph(g, back));
        CHECK(back.ToText() == t);
        CHECK(back.Find(2)->text == "assets/my brick%.png");
        // it still compiles and evaluates the same
        Program p1, p2; std::vector<Diagnostic> d;
        CHECK(g.Compile(&p1, &d)); CHECK(back.Compile(&p2, &d));
        MaterialOutputs a = EvaluateProgram(p1, EvalInput()), b = EvaluateProgram(p2, EvalInput());
        CHECK(a.baseColor[0] == b.baseColor[0] && a.roughness == b.roughness && a.metallic == b.metallic);

        // awkward text and numbers survive
        MaterialGraph h = MaterialGraph::MakeDefault();
        int pn = h.AddParam(NodeKind::ParamFloat, "p", 3.4028234e38f, -0.0f, 1e-38f, 123456.789f);
        h.Find(pn)->text = "~";
        h.Find(pn)->x = -1234.5f;
        int pm = h.AddParam(NodeKind::ParamFloat, "q"); h.Find(pm)->text = std::string("a b%c~\t\xC3\xA9", 9);
        int e = h.AddParam(NodeKind::ParamFloat, "r"); h.Find(e)->text = "";
        MaterialGraph hb; CHECK(MaterialGraph::FromText(h.ToText(), hb).ok);
        CHECK(SameGraph(h, hb));
        CHECK(hb.Find(pm)->text == h.Find(pm)->text);
        CHECK(hb.Find(pn)->text == "~");
        CHECK(hb.Find(e)->text.empty());

        // comments, blank lines, CRLF are fine
        std::string crlf = "# comment\r\n\r\nokaymatgraph 1\r\nnode 1 Output 0 0 0 0 0 0 ~\r\n# end\r\n";
        MaterialGraph c; CHECK(MaterialGraph::FromText(crlf, c).ok); CHECK(c.nodes.size() == 1);

        // file save and load
        std::string path = "test_material_graph_tmp.okaymat";
        CHECK(g.SaveToFile(path));
        MaterialGraph fl; CHECK(MaterialGraph::LoadFromFile(path, fl).ok); CHECK(SameGraph(g, fl));
        std::remove(path.c_str());
        CHECK(!MaterialGraph::LoadFromFile("no/such/file.okaymat", fl).ok);

        // telling the two kinds of .okaymat apart; the old reader is not hurt
        okay::Material m0;
        CHECK(MaterialGraph::LooksLikeGraph(t));
        CHECK(!MaterialGraph::LooksLikeGraph(m0.ToText()));
        CHECK(!MaterialGraph::LooksLikeGraph(""));
        okay::Material fromGraph = okay::Material::FromText(t);
        CHECK(fromGraph.ToText() == m0.ToText());              // unknown words are skipped: a plain default material
        okay::Material m1; m1.specular = 0.5f; m1.texture = "x.png";
        CHECK(okay::Material::FromText(m1.ToText()).ToText() == m1.ToText());
    }

    // ---- hostile input: crafted ----
    {
        MaterialGraph out;
        const char* bad[] = {
            "", "\n\n", "okaymat 1\n", "okaymatgraph 2\n", "okaymatgraph\n", "okaymatgraph 1 x\n",
            "node 1 Output 0 0 0 0 0 0 ~\n",                                  // no header
            "okaymatgraph 1\nnode 1 Output 0 0 0 0 0 0\n",                    // 9 tokens
            "okaymatgraph 1\nnode 1 Output 0 0 0 0 0 0 ~ extra\n",
            "okaymatgraph 1\nnode x Output 0 0 0 0 0 0 ~\n",
            "okaymatgraph 1\nnode -1 Output 0 0 0 0 0 0 ~\n",
            "okaymatgraph 1\nnode 99999999999 Output 0 0 0 0 0 0 ~\n",
            "okaymatgraph 1\nnode 1 Bogus 0 0 0 0 0 0 ~\n",
            "okaymatgraph 1\nnode 1 Output nan 0 0 0 0 0 ~\n",
            "okaymatgraph 1\nnode 1 Output 0 0 inf 0 0 0 ~\n",
            "okaymatgraph 1\nnode 1 Output 0 0 1e999 0 0 0 ~\n",
            "okaymatgraph 1\nnode 1 Output 0 0 0x10 0 0 0 ~\n",
            "okaymatgraph 1\nnode 1 Output 0 0 1.5abc 0 0 0 ~\n",
            "okaymatgraph 1\nnode 1 Output 0 0 0 0 0 0 %zz\n",
            "okaymatgraph 1\nnode 1 Output 0 0 0 0 0 0 abc%4\n",
            "okaymatgraph 1\nnode 1 Output 0 0 0 0 0 0 abc%\n",
            "okaymatgraph 1\nlink 1 2 3\n",
            "okaymatgraph 1\nlink 1 2 3 x\n",
            "okaymatgraph 1\nlink 1 2 3 -4\n",
            "okaymatgraph 1\nwhat is this\n",
            "okaymatgraph 1\nnode\n",
        };
        for (const char* s : bad) { MaterialGraph::ParseResult r = MaterialGraph::FromText(s, out); CHECK(!r.ok); CHECK(!r.error.empty()); }
        CHECK(MaterialGraph::FromText(std::string("okaymatgraph 1\nnode 1 Output 0 0 0 0 0 0 ") + std::string(5000, 'a') + "\n", out).ok == false);
        CHECK(MaterialGraph::FromText(std::string("okaymatgraph 1\n") + std::string(kMaxFileBytes + 1, '#'), out).ok == false);
        CHECK(MaterialGraph::FromText(std::string("okaymatgraph 1\nnode 1 Output 0 0 0 0 0 0 ", 41) + std::string(300, 'a') + "\n", out).ok == false);   // text longer than the limit
        std::string many = "okaymatgraph 1\n";
        for (int i = 0; i < kMaxNodes + 1; ++i) many += "node " + std::to_string(i) + " Time 0 0 0 0 0 0 ~\n";
        CHECK(!MaterialGraph::FromText(many, out).ok);
        many = "okaymatgraph 1\n";
        for (int i = 0; i < kMaxLinks + 1; ++i) many += "link 1 0 2 0\n";
        CHECK(!MaterialGraph::FromText(many, out).ok);
        std::string nul = "okaymatgraph 1\nnode 1 Out"; nul.push_back('\0'); nul += "put 0 0 0 0 0 0 ~\n";
        CHECK(!MaterialGraph::FromText(nul, out).ok);
        // a failed parse leaves the old graph alone
        MaterialGraph keep = Sample(); MaterialGraph copy = keep;
        CHECK(!MaterialGraph::FromText("junk", keep).ok); CHECK(SameGraph(keep, copy));
        // parses but the checker says no (the parser does not check meaning)
        CHECK(MaterialGraph::FromText("okaymatgraph 1\nlink 5 0 6 0\n", out).ok);
        CHECK(!Ok(out));
    }

    // ---- big graphs do not blow the stack ----
    {
        MaterialGraph g = MaterialGraph::MakeDefault();
        int prev = g.AddConst(1.0f);
        for (int i = 0; i < kMaxNodes - 3; ++i) {
            int a = g.AddNode(NodeKind::Add);
            Wire(g, prev, i == 0 ? "Value" : "Result", a, "A");
            prev = a;
        }
        Wire(g, prev, "Result", 1, "Roughness");
        Program p; std::vector<Diagnostic> d;
        CHECK(g.Compile(&p, &d));
        MaterialOutputs o = EvaluateProgram(p, EvalInput());
        CHECK_NEAR(o.roughness, 1.0, 1e-6);
    }

    // ---- hostile input: mutation fuzz (seeded, so a failure repeats) ----
    {
        std::mt19937 rng(12345);
        std::string seedText = Sample().ToText();
        int parsed = 0, compiled = 0;
        for (int iter = 0; iter < 20000; ++iter) {
            std::string s = seedText;
            int ops = 1 + (int)(rng() % 4);
            for (int k = 0; k < ops && !s.empty(); ++k) {
                size_t pos = rng() % s.size();
                switch (rng() % 7) {
                    case 0: s[pos] = (char)(rng() & 0xFF); break;
                    case 1: s.erase(pos, 1 + rng() % 8); break;
                    case 2: s.insert(pos, 1, (char)(rng() & 0xFF)); break;
                    case 3: s.resize(pos); break;
                    case 4: { size_t a = rng() % s.size(), b = std::min(s.size(), a + 1 + rng() % 40); s.insert(pos, s.substr(a, b - a)); break; }
                    case 5: { const char* w[] = {"nan", "inf", "-1", "99999", "1e39", "%", "~", "node", "link", " "}; s.insert(pos, w[rng() % 9]); break; }
                    default: s[pos] = "0123456789 \n"[rng() % 12]; break;
                }
            }
            MaterialGraph g;
            if (!MaterialGraph::FromText(s, g).ok) continue;
            ++parsed;
            std::vector<Diagnostic> d; Program p;
            if (g.Compile(&p, &d)) { ++compiled; EvaluateProgram(p, EvalInput()); }
        }
        CHECK(parsed > 200);       // the fuzz does reach the checker
        CHECK(compiled > 20);      // and sometimes the evaluator
        // pure noise
        for (int iter = 0; iter < 2000; ++iter) {
            std::string s(rng() % 300, ' ');
            for (char& c : s) c = (char)(rng() & 0xFF);
            MaterialGraph g; MaterialGraph::FromText(s, g);
            s = "okaymatgraph 1\n" + s;
            MaterialGraph::FromText(s, g);
        }
        // random structure: random links between real nodes, checker must never crash
        for (int iter = 0; iter < 3000; ++iter) {
            MaterialGraph g = MaterialGraph::MakeDefault();
            int n = 1 + (int)(rng() % 12);
            for (int i = 0; i < n; ++i) g.AddNode((NodeKind)(1 + rng() % ((int)NodeKind::Count - 1)));
            int nl = (int)(rng() % 20);
            for (int i = 0; i < nl; ++i) g.Connect((int)(rng() % (n + 3)), (int)(rng() % 4) - 1, (int)(rng() % (n + 3)), (int)(rng() % 7) - 1);
            std::vector<Diagnostic> d; Program p;
            if (g.Compile(&p, &d)) EvaluateProgram(p, EvalInput());
        }
    }

    TEST_MAIN_RESULT();
}
