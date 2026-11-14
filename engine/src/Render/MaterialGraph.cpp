// Material graph: node table, model helpers and the checker/lowering to IR.
// See docs/foundations/material_graph.md.
#include "okay/Render/MaterialGraph.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace okay::matgraph {

int PinComponents(PinType t) {
    switch (t) {
        case PinType::Float: return 1;
        case PinType::Vec2: return 2;
        case PinType::Vec3: return 3;
        case PinType::Vec4: return 4;
        default: return 0;
    }
}
const char* PinTypeName(PinType t) {
    switch (t) {
        case PinType::Float: return "float";
        case PinType::Vec2: return "vec2";
        case PinType::Vec3: return "vec3";
        case PinType::Vec4: return "vec4";
        case PinType::Texture: return "texture";
        case PinType::Sampler: return "sampler";
    }
    return "?";
}
bool IsNumeric(PinType t) { return PinComponents(t) > 0; }
bool CanConnect(PinType from, PinType to) {
    if (from == to) return true;
    return from == PinType::Float && IsNumeric(to);
}

const char* OpName(Op o) {
    static const char* n[] = {"Const", "Param", "TextureObj", "SamplerObj", "Uv", "WorldPos", "Normal", "View", "Time",
                              "Splat", "Rgb", "Alpha", "Add", "Mul", "Lerp", "Clamp", "Sat", "Pow", "OneMinus", "Dot",
                              "Norm", "Fresnel", "Panner", "Noise", "Sample"};
    return n[(int)o];
}

// ---- the node table -------------------------------------------------------------------
namespace {
using P = PinDef;
constexpr PinType F = PinType::Float, V2 = PinType::Vec2, V3 = PinType::Vec3, V4 = PinType::Vec4;

P in(const char* n, PinType t, DefKind d = DefKind::Required, float c0 = 0, float c1 = 0, float c2 = 0, float c3 = 0) {
    return P{n, t, false, d, {c0, c1, c2, c3}};
}
P gen(const char* n, DefKind d, float c0 = 0) { return P{n, F, true, d, {c0, 0, 0, 0}}; }
P out(const char* n, PinType t) { return P{n, t, false, DefKind::Required, {0, 0, 0, 0}}; }
P genOut(const char* n) { return P{n, F, true, DefKind::Required, {0, 0, 0, 0}}; }

NodeDef D(NodeKind k, const char* name, const char* cat, std::vector<P> i, std::vector<P> o, int vc = 0,
          bool text = false, bool nl = false) {
    return NodeDef{k, name, cat, std::move(i), std::move(o), vc, text, nl};
}

const std::vector<NodeDef>& Table() {
    static const std::vector<NodeDef> t = [] {
        std::vector<NodeDef> v;
        auto C = DefKind::Const;
        v.push_back(D(NodeKind::Output, "Output", "Result",
                      {in("BaseColor", V3, C, 0.8f, 0.8f, 0.8f), in("Metallic", F, C, 0), in("Roughness", F, C, 0.5f),
                       in("Emissive", V3, C, 0, 0, 0), in("Opacity", F, C, 1), in("Normal", V3, C, 0, 0, 1)},
                      {}));
        v.push_back(D(NodeKind::ConstFloat, "ConstFloat", "Constants", {}, {out("Value", F)}, 1));
        v.push_back(D(NodeKind::ConstVec2, "ConstVec2", "Constants", {}, {out("Value", V2)}, 2));
        v.push_back(D(NodeKind::ConstVec3, "ConstVec3", "Constants", {}, {out("Value", V3)}, 3));
        v.push_back(D(NodeKind::ConstVec4, "ConstVec4", "Constants", {}, {out("Value", V4)}, 4));
        v.push_back(D(NodeKind::ParamFloat, "ParamFloat", "Parameters", {}, {out("Value", F)}, 1, true));
        v.push_back(D(NodeKind::ParamVec2, "ParamVec2", "Parameters", {}, {out("Value", V2)}, 2, true));
        v.push_back(D(NodeKind::ParamVec3, "ParamVec3", "Parameters", {}, {out("Value", V3)}, 3, true));
        v.push_back(D(NodeKind::ParamVec4, "ParamVec4", "Parameters", {}, {out("Value", V4)}, 4, true));
        v.push_back(D(NodeKind::TextureObject, "TextureObject", "Textures", {}, {out("Texture", PinType::Texture)}, 0, true));
        v.push_back(D(NodeKind::SamplerState, "SamplerState", "Textures", {}, {out("Sampler", PinType::Sampler)}, 1));
        v.push_back(D(NodeKind::TextureSample, "TextureSample", "Textures",
                      {in("Texture", PinType::Texture), in("Sampler", PinType::Sampler, C, 0),
                       in("UV", V2, DefKind::UV)},
                      {out("RGB", V3), out("A", F), out("RGBA", V4)}));
        v.push_back(D(NodeKind::TexCoord, "TexCoord", "Inputs", {}, {out("UV", V2)}));
        v.push_back(D(NodeKind::WorldPosition, "WorldPosition", "Inputs", {}, {out("Position", V3)}));
        v.push_back(D(NodeKind::WorldNormal, "WorldNormal", "Inputs", {}, {out("Normal", V3)}));
        v.push_back(D(NodeKind::ViewVector, "ViewVector", "Inputs", {}, {out("View", V3)}));
        v.push_back(D(NodeKind::Time, "Time", "Inputs", {}, {out("Time", F)}));
        v.push_back(D(NodeKind::Add, "Add", "Math", {gen("A", C, 0), gen("B", C, 0)}, {genOut("Result")}));
        v.push_back(D(NodeKind::Multiply, "Multiply", "Math", {gen("A", C, 1), gen("B", C, 1)}, {genOut("Result")}));
        v.push_back(D(NodeKind::Lerp, "Lerp", "Math", {gen("A", C, 0), gen("B", C, 1), gen("Alpha", C, 0.5f)}, {genOut("Result")}));
        v.push_back(D(NodeKind::Clamp, "Clamp", "Math", {gen("X", C, 0), gen("Min", C, 0), gen("Max", C, 1)}, {genOut("Result")}));
        v.push_back(D(NodeKind::Saturate, "Saturate", "Math", {gen("X", C, 0)}, {genOut("Result")}));
        v.push_back(D(NodeKind::Power, "Power", "Math", {gen("Base", C, 1), gen("Exp", C, 2)}, {genOut("Result")}));
        v.push_back(D(NodeKind::OneMinus, "OneMinus", "Math", {gen("X", C, 0)}, {genOut("Result")}));
        v.push_back(D(NodeKind::Dot, "Dot", "Math", {gen("A", DefKind::Required), gen("B", DefKind::Required)}, {out("Result", F)}));
        v.push_back(D(NodeKind::Normalize, "Normalize", "Math", {gen("X", DefKind::Required)}, {genOut("Result")}, 0, false, true));
        v.push_back(D(NodeKind::Fresnel, "Fresnel", "Effects",
                      {in("Exponent", F, C, 5), in("BaseReflect", F, C, 0.04f), in("Normal", V3, DefKind::WorldNormal),
                       in("View", V3, DefKind::ViewVector)},
                      {out("Result", F)}));
        v.push_back(D(NodeKind::Panner, "Panner", "Effects",
                      {in("UV", V2, DefKind::UV), in("Speed", V2, C, 0, 0), in("Time", F, DefKind::Time)}, {out("UV", V2)}));
        v.push_back(D(NodeKind::Noise, "Noise", "Effects", {in("Position", V2, DefKind::UV), in("Scale", F, C, 8)},
                      {out("Value", F)}));
        return v;
    }();
    return t;
}
} // namespace

const NodeDef& GetNodeDef(NodeKind k) { return Table()[(size_t)k]; }
bool NodeKindFromName(const std::string& name, NodeKind& out) {
    for (const NodeDef& d : Table()) if (name == d.name) { out = d.kind; return true; }
    return false;
}
int InPinIndex(NodeKind k, const std::string& pin) {
    const NodeDef& d = GetNodeDef(k);
    for (size_t i = 0; i < d.in.size(); ++i) if (pin == d.in[i].name) return (int)i;
    return -1;
}
int OutPinIndex(NodeKind k, const std::string& pin) {
    const NodeDef& d = GetNodeDef(k);
    for (size_t i = 0; i < d.out.size(); ++i) if (pin == d.out[i].name) return (int)i;
    return -1;
}

// ---- model helpers ------------------------------------------------------------------------
int MaterialGraph::AddNode(NodeKind k, float x, float y) {
    int id = 1;
    for (const Node& n : nodes) id = std::max(id, n.id + 1);
    Node n; n.id = id; n.kind = k; n.x = x; n.y = y;
    nodes.push_back(n);
    return id;
}
int MaterialGraph::AddConst(float a) { int id = AddNode(NodeKind::ConstFloat); Find(id)->v[0] = a; return id; }
int MaterialGraph::AddConst(float a, float b, float c) {
    int id = AddNode(NodeKind::ConstVec3); Node* n = Find(id); n->v[0] = a; n->v[1] = b; n->v[2] = c; return id;
}
int MaterialGraph::AddParam(NodeKind k, const std::string& name, float a, float b, float c, float d) {
    int id = AddNode(k); Node* n = Find(id);
    n->text = name; n->v[0] = a; n->v[1] = b; n->v[2] = c; n->v[3] = d;
    return id;
}
Node* MaterialGraph::Find(int id) { for (Node& n : nodes) if (n.id == id) return &n; return nullptr; }
const Node* MaterialGraph::Find(int id) const { for (const Node& n : nodes) if (n.id == id) return &n; return nullptr; }
void MaterialGraph::Connect(int fn, int fp, int tn, int tp) { links.push_back(Link{fn, fp, tn, tp}); }
MaterialGraph MaterialGraph::MakeDefault() { MaterialGraph g; g.AddNode(NodeKind::Output); return g; }

// ---- checker and lowering ---------------------------------------------------------------------
namespace {

bool ValidIdent(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (i > 0 && c >= '0' && c <= '9');
        if (!ok) return false;
    }
    return true;
}

class Analyzer {
public:
    Analyzer(const MaterialGraph& g, std::vector<Diagnostic>* d, Program* p) : g_(g), diags_(d), prog_(p) {}

    bool Run() {
        if ((int)g_.nodes.size() > kMaxNodes) { Err("limit", -1, -1, "too many nodes"); return false; }
        if ((int)g_.links.size() > kMaxLinks) { Err("limit", -1, -1, "too many links"); return false; }
        CheckNodes();
        CheckLinks();
        CheckOutputCount();
        if (!errors_) CheckCycles();
        if (!errors_) Lower();
        return errors_ == 0;
    }

private:
    const MaterialGraph& g_;
    std::vector<Diagnostic>* diags_;
    Program* prog_;
    int errors_ = 0;
    std::unordered_map<int, int> idx_;                 // node id -> index in g_.nodes
    std::vector<std::vector<int>> inLink_;             // per node index, per in pin: link index or -1
    std::vector<int> order_;                           // topological order (node indices)
    std::vector<IrOp> ops_;
    std::vector<std::vector<int>> outVal_;             // per node index, per out pin: op index or -1
    int curNode_ = -1;

    void Add(Severity s, const char* code, int node, int pin, const std::string& msg) {
        if (s == Severity::Error) ++errors_;
        if (diags_) diags_->push_back(Diagnostic{s, code, node, pin, msg});
    }
    void Err(const char* code, int node, int pin, const std::string& msg) { Add(Severity::Error, code, node, pin, msg); }

    void CheckNodes() {
        std::map<std::string, int> paramNames;
        for (size_t i = 0; i < g_.nodes.size(); ++i) {
            const Node& n = g_.nodes[i];
            if (idx_.count(n.id)) { Err("duplicate-id", n.id, -1, "two nodes share this id"); continue; }
            idx_[n.id] = (int)i;
            if ((int)n.kind >= (int)NodeKind::Count) { Err("bad-kind", n.id, -1, "unknown node kind"); continue; }
            const NodeDef& d = GetNodeDef(n.kind);
            for (int k = 0; k < 4; ++k)
                if (!std::isfinite(n.v[k])) { Err("bad-value", n.id, -1, "a number is not finite"); break; }
            if (!std::isfinite(n.x) || !std::isfinite(n.y)) Err("bad-value", n.id, -1, "position is not finite");
            if (n.kind == NodeKind::ParamFloat || n.kind == NodeKind::ParamVec2 || n.kind == NodeKind::ParamVec3 ||
                n.kind == NodeKind::ParamVec4) {
                if (!ValidIdent(n.text)) Err("bad-param-name", n.id, -1, "parameter name must be a letter or _ then letters, digits, _ (at most 64)");
                else if (paramNames[n.text]++) Err("duplicate-param", n.id, -1, "parameter name '" + n.text + "' is used twice");
            }
            if (n.kind == NodeKind::TextureObject) {
                bool bad = n.text.empty() || n.text.size() > kMaxTextLen;
                for (unsigned char c : n.text) if (c < 0x20 || c == 0x7f) bad = true;
                if (bad) Err("bad-texture-path", n.id, -1, "texture path is empty, too long or has control characters");
            }
            if (n.kind == NodeKind::SamplerState) {
                float m = n.v[0];
                if (!(m >= 0 && m < kSamplerModeCount) || m != std::floor(m)) Err("bad-sampler", n.id, -1, "sampler mode must be 0, 1, 2 or 3");
            }
            (void)d;
        }
    }

    void CheckLinks() {
        inLink_.assign(g_.nodes.size(), {});
        for (size_t i = 0; i < g_.nodes.size(); ++i) {
            int k = (int)g_.nodes[i].kind;
            if (k >= 0 && k < (int)NodeKind::Count) inLink_[i].assign(GetNodeDef(g_.nodes[i].kind).in.size(), -1);
        }
        for (size_t li = 0; li < g_.links.size(); ++li) {
            const Link& l = g_.links[li];
            auto f = idx_.find(l.fromNode), t = idx_.find(l.toNode);
            if (f == idx_.end() || t == idx_.end()) { Err("bad-link", -1, -1, "a link points at a node that does not exist"); continue; }
            const Node& fn = g_.nodes[f->second]; const Node& tn = g_.nodes[t->second];
            if ((int)fn.kind >= (int)NodeKind::Count || (int)tn.kind >= (int)NodeKind::Count) continue;
            const NodeDef& fd = GetNodeDef(fn.kind); const NodeDef& td = GetNodeDef(tn.kind);
            if (l.fromPin < 0 || l.fromPin >= (int)fd.out.size()) { Err("bad-link", fn.id, l.fromPin, "output pin number out of range"); continue; }
            if (l.toPin < 0 || l.toPin >= (int)td.in.size()) { Err("bad-link", tn.id, l.toPin, "input pin number out of range"); continue; }
            int& slot = inLink_[t->second][l.toPin];
            if (slot >= 0) { Err("duplicate-link", tn.id, l.toPin, std::string("input '") + td.in[l.toPin].name + "' has two links"); continue; }
            slot = (int)li;
        }
        for (size_t i = 0; i < g_.nodes.size(); ++i) {
            if (inLink_[i].empty()) continue;
            const NodeDef& d = GetNodeDef(g_.nodes[i].kind);
            for (size_t p = 0; p < d.in.size(); ++p)
                if (inLink_[i][p] < 0 && d.in[p].def == DefKind::Required)
                    Err("missing-input", g_.nodes[i].id, (int)p, std::string(d.name) + " needs input '" + d.in[p].name + "'");
        }
    }

    void CheckOutputCount() {
        int n = 0;
        for (const Node& x : g_.nodes) if (x.kind == NodeKind::Output) ++n;
        if (n == 0) Err("no-output", -1, -1, "the graph has no Output node");
        else if (n > 1) Err("many-outputs", -1, -1, "the graph has " + std::to_string(n) + " Output nodes; exactly one is allowed");
    }

    void CheckCycles() {
        size_t n = g_.nodes.size();
        std::vector<int> indeg(n, 0);
        std::vector<std::vector<int>> adj(n);
        for (const Link& l : g_.links) {
            int f = idx_[l.fromNode], t = idx_[l.toNode];
            adj[f].push_back(t); ++indeg[t];
        }
        std::vector<int> stack;
        for (size_t i = 0; i < n; ++i) if (!indeg[i]) stack.push_back((int)i);
        while (!stack.empty()) {
            int u = stack.back(); stack.pop_back();
            order_.push_back(u);
            for (int v : adj[u]) if (--indeg[v] == 0) stack.push_back(v);
        }
        if (order_.size() != n) {
            for (size_t i = 0; i < n; ++i) if (indeg[i] > 0) { Err("cycle", g_.nodes[i].id, -1, "the links make a loop"); break; }
        }
    }

    // ---- lowering ----
    int Emit(Op op, PinType t, int a = -1, int b = -1, int c = -1, int d = -1) {
        IrOp o; o.op = op; o.type = t; o.a = a; o.b = b; o.c = c; o.d = d; o.node = curNode_;
        ops_.push_back(o);
        return (int)ops_.size() - 1;
    }
    int EmitConst(PinType t, const float* v) {
        int i = Emit(Op::Const, t);
        for (int k = 0; k < PinComponents(t); ++k) ops_[i].v[k] = v[k];
        return i;
    }
    int Splat(int val, PinType to) {
        if (ops_[val].type == to) return val;
        return Emit(Op::Splat, to, val);
    }

    /// Value feeding input pin `p` of node index `ni`; -1 if it cannot be made (error already reported).
    int Fetch(int ni, int p) {
        const NodeDef& d = GetNodeDef(g_.nodes[ni].kind);
        const PinDef& pd = d.in[p];
        int li = inLink_[ni][p];
        if (li >= 0) {
            const Link& l = g_.links[li];
            return outVal_[idx_[l.fromNode]][l.fromPin];
        }
        switch (pd.def) {
            case DefKind::Required: return -1;
            case DefKind::Const:
                if (pd.type == PinType::Sampler) { int i = Emit(Op::SamplerObj, PinType::Sampler); ops_[i].imm = (int)pd.c[0]; return i; }
                return EmitConst(pd.type, pd.c);
            case DefKind::UV: return Emit(Op::Uv, V2);
            case DefKind::WorldNormal: return Emit(Op::Normal, V3);
            case DefKind::ViewVector: return Emit(Op::View, V3);
            case DefKind::Time: return Emit(Op::Time, F);
        }
        return -1;
    }
    static constexpr PinType V2 = PinType::Vec2, V3 = PinType::Vec3, F = PinType::Float;

    /// Fetch for a non-generic pin: checks the type and widens a Float.
    int FetchConcrete(int ni, int p) {
        int v = Fetch(ni, p);
        if (v < 0) return -1;
        const PinDef& pd = GetNodeDef(g_.nodes[ni].kind).in[p];
        PinType have = ops_[v].type;
        if (!CanConnect(have, pd.type)) {
            Err("type", g_.nodes[ni].id, p, std::string("input '") + pd.name + "' needs " + PinTypeName(pd.type) +
                " but gets " + PinTypeName(have));
            return -1;
        }
        return Splat(v, pd.type);
    }

    void Lower() {
        outVal_.assign(g_.nodes.size(), {});
        for (int ni : order_) {
            const Node& n = g_.nodes[ni];
            curNode_ = n.id;
            outVal_[ni].assign(GetNodeDef(n.kind).out.size(), -1);
            LowerNode(ni);
        }
        if (errors_) return;
        Prune(prog_);
    }

    void LowerNode(int ni) {
        const Node& n = g_.nodes[ni];
        std::vector<int>& ov = outVal_[ni];
        switch (n.kind) {
            case NodeKind::Output: {
                int vals[kOutputPinCount];
                for (int p = 0; p < kOutputPinCount; ++p) vals[p] = FetchConcrete(ni, p);
                for (int p = 0; p < kOutputPinCount; ++p) outputs_[p] = vals[p];
                hasOutput_ = true; outputNode_ = n.id;
                break;
            }
            case NodeKind::ConstFloat: case NodeKind::ConstVec2: case NodeKind::ConstVec3: case NodeKind::ConstVec4:
                ov[0] = EmitConst(GetNodeDef(n.kind).out[0].type, n.v); break;
            case NodeKind::ParamFloat: case NodeKind::ParamVec2: case NodeKind::ParamVec3: case NodeKind::ParamVec4: {
                PinType t = GetNodeDef(n.kind).out[0].type;
                int i = Emit(Op::Param, t);
                ops_[i].s = n.text;
                for (int k = 0; k < PinComponents(t); ++k) ops_[i].v[k] = n.v[k];
                ov[0] = i; break;
            }
            case NodeKind::TextureObject: { int i = Emit(Op::TextureObj, PinType::Texture); ops_[i].s = n.text; ov[0] = i; break; }
            case NodeKind::SamplerState: { int i = Emit(Op::SamplerObj, PinType::Sampler); ops_[i].imm = (int)n.v[0]; ov[0] = i; break; }
            case NodeKind::TextureSample: {
                int t = FetchConcrete(ni, 0), s = FetchConcrete(ni, 1), uv = FetchConcrete(ni, 2);
                if (t < 0 || s < 0 || uv < 0) break;
                int smp = Emit(Op::Sample, PinType::Vec4, t, s, uv);
                ov[0] = Emit(Op::Rgb, V3, smp);
                ov[1] = Emit(Op::Alpha, F, smp);
                ov[2] = smp;
                break;
            }
            case NodeKind::TexCoord: ov[0] = Emit(Op::Uv, V2); break;
            case NodeKind::WorldPosition: ov[0] = Emit(Op::WorldPos, V3); break;
            case NodeKind::WorldNormal: ov[0] = Emit(Op::Normal, V3); break;
            case NodeKind::ViewVector: ov[0] = Emit(Op::View, V3); break;
            case NodeKind::Time: ov[0] = Emit(Op::Time, F); break;
            case NodeKind::Fresnel: {
                int e = FetchConcrete(ni, 0), f0 = FetchConcrete(ni, 1), nn = FetchConcrete(ni, 2), vv = FetchConcrete(ni, 3);
                if (e < 0 || f0 < 0 || nn < 0 || vv < 0) break;
                ov[0] = Emit(Op::Fresnel, F, nn, vv, e, f0);
                break;
            }
            case NodeKind::Panner: {
                int uv = FetchConcrete(ni, 0), sp = FetchConcrete(ni, 1), t = FetchConcrete(ni, 2);
                if (uv < 0 || sp < 0 || t < 0) break;
                ov[0] = Emit(Op::Panner, V2, uv, sp, t);
                break;
            }
            case NodeKind::Noise: {
                int pos = FetchConcrete(ni, 0), sc = FetchConcrete(ni, 1);
                if (pos < 0 || sc < 0) break;
                ov[0] = Emit(Op::Noise, F, pos, sc);
                break;
            }
            default: LowerGeneric(ni); break;
        }
    }

    void LowerGeneric(int ni) {
        const Node& n = g_.nodes[ni];
        const NodeDef& d = GetNodeDef(n.kind);
        std::vector<int> v(d.in.size(), -1);
        int common = 1; bool bad = false;
        for (size_t p = 0; p < d.in.size(); ++p) {
            v[p] = Fetch(ni, (int)p);
            if (v[p] < 0) { bad = true; continue; }
            PinType t = ops_[v[p]].type;
            if (!IsNumeric(t)) {
                Err("type", n.id, (int)p, std::string("input '") + d.in[p].name + "' needs a number or vector but gets " + PinTypeName(t));
                bad = true; continue;
            }
            common = std::max(common, PinComponents(t));
        }
        if (bad) return;
        PinType T = common == 1 ? PinType::Float : common == 2 ? PinType::Vec2 : common == 3 ? PinType::Vec3 : PinType::Vec4;
        for (size_t p = 0; p < d.in.size(); ++p) {
            PinType t = ops_[v[p]].type;
            if (t != T && t != PinType::Float) {
                Err("type", n.id, (int)p, std::string("input '") + d.in[p].name + "' is " + PinTypeName(t) + " but the node works on " +
                    PinTypeName(T) + " (only a float may be widened)");
                bad = true;
            }
        }
        if (bad) return;
        if (d.normalizeLike && T == PinType::Float) { Err("type", n.id, 0, "Normalize needs a vec2, vec3 or vec4"); return; }
        for (size_t p = 0; p < d.in.size(); ++p) v[p] = Splat(v[p], T);
        int r = -1;
        switch (n.kind) {
            case NodeKind::Add: r = Emit(Op::Add, T, v[0], v[1]); break;
            case NodeKind::Multiply: r = Emit(Op::Mul, T, v[0], v[1]); break;
            case NodeKind::Lerp: r = Emit(Op::Lerp, T, v[0], v[1], v[2]); break;
            case NodeKind::Clamp: r = Emit(Op::Clamp, T, v[0], v[1], v[2]); break;
            case NodeKind::Saturate: r = Emit(Op::Sat, T, v[0]); break;
            case NodeKind::Power: r = Emit(Op::Pow, T, v[0], v[1]); break;
            case NodeKind::OneMinus: r = Emit(Op::OneMinus, T, v[0]); break;
            case NodeKind::Dot: r = Emit(Op::Dot, PinType::Float, v[0], v[1]); break;
            case NodeKind::Normalize: r = Emit(Op::Norm, T, v[0]); break;
            default: break;
        }
        outVal_[ni][0] = r;
    }

    int outputs_[kOutputPinCount] = {-1, -1, -1, -1, -1, -1};
    bool hasOutput_ = false;
    int outputNode_ = -1;

    /// Drops ops that do not reach an output; reports unused nodes; copies into *p when non-null.
    void Prune(Program* p) {
        std::vector<char> live(ops_.size(), 0);
        std::vector<int> st;
        for (int o : outputs_) if (o >= 0) st.push_back(o);
        while (!st.empty()) {
            int i = st.back(); st.pop_back();
            if (live[i]) continue;
            live[i] = 1;
            for (int x : {ops_[i].a, ops_[i].b, ops_[i].c, ops_[i].d}) if (x >= 0) st.push_back(x);
        }
        std::map<int, bool> nodeLive;
        for (size_t i = 0; i < ops_.size(); ++i) if (live[i]) nodeLive[ops_[i].node] = true;
        for (const Node& n : g_.nodes)
            if (n.kind != NodeKind::Output && !nodeLive[n.id]) Add(Severity::Info, "unused", n.id, -1, "this node does not reach the Output");
        if (!p) return;
        std::vector<int> remap(ops_.size(), -1);
        p->ops.clear();
        for (size_t i = 0; i < ops_.size(); ++i) {
            if (!live[i]) continue;
            IrOp o = ops_[i];
            if (o.a >= 0) o.a = remap[o.a];
            if (o.b >= 0) o.b = remap[o.b];
            if (o.c >= 0) o.c = remap[o.c];
            if (o.d >= 0) o.d = remap[o.d];
            remap[i] = (int)p->ops.size();
            p->ops.push_back(o);
        }
        for (int k = 0; k < kOutputPinCount; ++k) p->out[k] = outputs_[k] >= 0 ? remap[outputs_[k]] : -1;
    }
};

} // namespace

bool MaterialGraph::Compile(Program* prog, std::vector<Diagnostic>* diags) const {
    Analyzer a(*this, diags, prog);
    return a.Run();
}

} // namespace okay::matgraph
