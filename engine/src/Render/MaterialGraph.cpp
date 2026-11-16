#include "okay/Render/MaterialGraph.hpp"
#include "NumParse.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace okay {
namespace matgraph {

// ---------------------------------------------------------------- node library

namespace {

using P = PinType;

PinDef In(const char* n, P t, float a = 0, float b = 0, float c = 0, float d = 0, int implicit = 0) {
    PinDef p{n, t, {a, b, c, d}}; p.implicit = implicit; return p;
}
PinDef Out(const char* n, P t) { return PinDef{n, t, {0, 0, 0, 0}}; }

std::vector<NodeDef> BuildLibrary() {
    std::vector<NodeDef> L;
    auto add = [&](const char* type, const char* cat, const char* help, std::vector<PinDef> in, std::vector<PinDef> out,
                   bool hasValue = false, bool hasPath = false) {
        L.push_back(NodeDef{type, cat, help, std::move(in), std::move(out), hasValue, hasPath});
    };
    // Input
    add("Float", "Input", "A single number.", {}, {Out("Value", P::Float)}, true);
    add("Vec2", "Input", "Two numbers (x, y).", {}, {Out("Value", P::Vec2)}, true);
    add("Vec3", "Input", "Three numbers (x, y, z), also used as a colour (r, g, b).", {}, {Out("Value", P::Vec3)}, true);
    add("UV", "Input", "The texture coordinate of the surface point.", {}, {Out("UV", P::Vec2)});
    add("Time", "Input", "Seconds since the game started.", {}, {Out("Time", P::Float)});
    add("Normal", "Input", "The surface normal (a unit vector).", {}, {Out("Normal", P::Vec3)});
    add("ViewDir", "Input", "The unit vector from the surface towards the camera.", {}, {Out("ViewDir", P::Vec3)});
    // Texture
    add("TextureSample", "Texture", "Reads a colour from an image at a texture coordinate (UV unwired: the surface UV).",
        {In("UV", P::Vec2, 0, 0, 0, 0, 1)}, {Out("RGBA", P::Vec4), Out("RGB", P::Vec3), Out("A", P::Float)}, false, true);
    add("NormalMap", "Texture", "Reads a tangent-space normal map; Strength scales the bumps. Wire the result to the Output Normal.",
        {In("UV", P::Vec2, 0, 0, 0, 0, 1), In("Strength", P::Float, 1)}, {Out("Normal", P::Vec3)}, false, true);
    // Math
    add("Add", "Math", "A + B, per component.", {In("A", P::Any), In("B", P::Any)}, {Out("Result", P::Any)});
    add("Subtract", "Math", "A - B, per component.", {In("A", P::Any), In("B", P::Any)}, {Out("Result", P::Any)});
    add("Multiply", "Math", "A * B, per component.", {In("A", P::Any, 1), In("B", P::Any, 1)}, {Out("Result", P::Any)});
    add("Divide", "Math", "A / B, per component (the CPU gives 0 when B is 0; a GPU gives infinity).",
        {In("A", P::Any), In("B", P::Any, 1)}, {Out("Result", P::Any)});
    add("Lerp", "Math", "Blends A to B: A + (B - A) * T.", {In("A", P::Any, 0), In("B", P::Any, 1), In("T", P::Any, 0.5f)},
        {Out("Result", P::Any)});
    add("Clamp", "Math", "Keeps Value between Min and Max.", {In("Value", P::Any), In("Min", P::Any, 0), In("Max", P::Any, 1)},
        {Out("Result", P::Any)});
    add("Saturate", "Math", "Clamp to the range 0 to 1.", {In("In", P::Any)}, {Out("Result", P::Any)});
    add("Pow", "Math", "Base to the power Exp (a negative Base counts as 0).", {In("Base", P::Any, 1), In("Exp", P::Any, 1)},
        {Out("Result", P::Any)});
    add("Dot", "Math", "Dot product of A and B: one number.", {In("A", P::Any), In("B", P::Any)}, {Out("Result", P::Float)});
    add("Normalize", "Math", "Scales a vector to length 1 (a zero vector stays zero).", {In("In", P::Any)}, {Out("Result", P::Any)});
    add("OneMinus", "Math", "1 - In, per component.", {In("In", P::Any)}, {Out("Result", P::Any)});
    add("Sin", "Math", "Sine of In (radians), per component.", {In("In", P::Any)}, {Out("Result", P::Any)});
    add("MakeVec3", "Math", "Builds a Vec3 from three numbers.", {In("X", P::Float), In("Y", P::Float), In("Z", P::Float)},
        {Out("Result", P::Vec3)});
    add("Split", "Math", "Takes a value apart into its numbers (missing ones are 0).", {In("In", P::Any)},
        {Out("X", P::Float), Out("Y", P::Float), Out("Z", P::Float), Out("W", P::Float)});
    // Effect
    add("Fresnel", "Effect", "Bright at grazing angles: (1 - Normal.View) to the power Power. Normal unwired: the surface normal.",
        {In("Normal", P::Vec3, 0, 0, 1, 0, 2), In("Power", P::Float, 5)}, {Out("Result", P::Float)});
    add("Noise", "Effect", "Smooth value noise between 0 and 1 (UV unwired: the surface UV). Scale is cells per UV unit.",
        {In("UV", P::Vec2, 0, 0, 0, 0, 1), In("Scale", P::Float, 8)}, {Out("Result", P::Float)});
    // Colour
    add("Desaturate", "Colour", "Blends a colour towards its grey (Rec.709 weights); Amount 1 is fully grey.",
        {In("Colour", P::Vec3, 1, 1, 1), In("Amount", P::Float, 1)}, {Out("Result", P::Vec3)});
    add("Contrast", "Colour", "(In - 0.5) * Amount + 0.5.", {In("In", P::Any, 0.5f), In("Amount", P::Any, 1)},
        {Out("Result", P::Any)});
    // Output
    add("Output", "Output", "The surface the material gives the renderer. Exactly one per graph.",
        {In("BaseColor", P::Vec3, 0.8f, 0.8f, 0.8f), In("Emission", P::Vec3, 0, 0, 0), In("Roughness", P::Float, 0.5f),
         In("Metallic", P::Float, 0), In("Alpha", P::Float, 1), In("Normal", P::Vec3, 0, 0, 1)},
        {});
    return L;
}

int PinIndex(const std::vector<PinDef>& pins, const std::string& name) {
    for (std::size_t i = 0; i < pins.size(); ++i) if (name == pins[i].name) return (int)i;
    return -1;
}

}  // namespace

const std::vector<NodeDef>& NodeLibrary() {
    static const std::vector<NodeDef> lib = BuildLibrary();
    return lib;
}

const NodeDef* FindNodeDef(const std::string& type) {
    for (const NodeDef& d : NodeLibrary()) if (type == d.type) return &d;
    return nullptr;
}

// ---------------------------------------------------------------- graph editing

int Graph::NextId() const {
    int m = 0;
    for (const Node& n : nodes) m = std::max(m, n.id);
    return m + 1;
}
Node* Graph::Find(int id) { for (Node& n : nodes) if (n.id == id) return &n; return nullptr; }
const Node* Graph::Find(int id) const { for (const Node& n : nodes) if (n.id == id) return &n; return nullptr; }

int Graph::Add(const std::string& type, float px, float py) {
    if (!FindNodeDef(type)) return 0;
    Node n; n.id = NextId(); n.type = type; n.posX = px; n.posY = py;
    nodes.push_back(n);
    return n.id;
}
int Graph::AddValue(const std::string& type, float x, float y, float z, float w) {
    int id = Add(type);
    if (Node* n = Find(id)) { n->value[0] = x; n->value[1] = y; n->value[2] = z; n->value[3] = w; }
    return id;
}
void Graph::Connect(int fn, const std::string& fp, int tn, const std::string& tp) {
    for (Link& l : links) if (l.toNode == tn && l.toPin == tp) { l.fromNode = fn; l.fromPin = fp; return; }
    Link l; l.fromNode = fn; l.fromPin = fp; l.toNode = tn; l.toPin = tp;
    links.push_back(l);
}
void Graph::SetDefault(int node, const std::string& pin, float x, float y, float z, float w) {
    if (Node* n = Find(node)) n->defaults[pin] = {x, y, z, w};
}
int Graph::OutputNode() const {
    for (const Node& n : nodes) if (n.type == "Output") return n.id;
    return 0;
}

// ---------------------------------------------------------------- text file

namespace {

std::string Num(float f) {
    char b[48]; std::snprintf(b, sizeof(b), "%.9g", (double)f);
    for (char* p = b; *p; ++p) if (*p == ',') *p = '.';
    return b;
}
std::string Vec4Text(const float* v) { return Num(v[0]) + "," + Num(v[1]) + "," + Num(v[2]) + "," + Num(v[3]); }

bool ParseFloat(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    float f = detail::StrToF(s.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(f)) return false;
    out = f; return true;
}
bool ParseFloats4(const std::string& s, float* out) {
    float tmp[4] = {0, 0, 0, 0};
    std::size_t pos = 0; int n = 0;
    while (pos <= s.size()) {
        std::size_t e = s.find(',', pos);
        if (e == std::string::npos) e = s.size();
        if (n >= 4 || !ParseFloat(s.substr(pos, e - pos), tmp[n])) return false;
        ++n; pos = e + 1;
    }
    if (n != 4) return false;
    for (int i = 0; i < 4; ++i) out[i] = tmp[i];
    return true;
}
bool ParseInt(const std::string& s, int& out) {
    if (s.empty() || s.size() > 9) return false;
    int v = 0;
    for (char c : s) { if (c < '0' || c > '9') return false; v = v * 10 + (c - '0'); }
    out = v; return true;
}
bool SplitRef(const std::string& s, int& id, std::string& pin) {
    std::size_t dot = s.find('.');
    if (dot == std::string::npos || dot + 1 >= s.size()) return false;
    if (!ParseInt(s.substr(0, dot), id)) return false;
    pin = s.substr(dot + 1);
    return true;
}
std::vector<std::string> Tokens(const std::string& line) {
    std::vector<std::string> t; std::string cur;
    for (char c : line) {
        if (c == ' ' || c == '\t') { if (!cur.empty()) { t.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) t.push_back(cur);
    return t;
}

}  // namespace

std::string Graph::ToText() const {
    std::ostringstream o;
    o << "okaymatgraph 1\n";
    for (const Node& n : nodes) {
        const NodeDef* d = FindNodeDef(n.type);
        o << "node " << n.id << " " << n.type;
        if (d && d->hasValue) o << " v=" << Vec4Text(n.value);
        o << " pos=" << Num(n.posX) << "," << Num(n.posY);
        if (d && d->hasPath && !n.path.empty()) {
            std::string p = n.path;
            for (char& c : p) if (c == '\n' || c == '\r') c = ' ';
            o << " path=" << p;   // last on the line: the path may hold spaces
        }
        o << "\n";
    }
    for (const Node& n : nodes)
        for (const auto& kv : n.defaults) o << "def " << n.id << "." << kv.first << " " << Vec4Text(kv.second.data()) << "\n";
    for (const Link& l : links) o << "link " << l.fromNode << "." << l.fromPin << " " << l.toNode << "." << l.toPin << "\n";
    return o.str();
}

bool Graph::FromText(const std::string& text, Graph& out, std::string* err) {
    auto fail = [&](int line, const std::string& m) {
        if (err) *err = "line " + std::to_string(line) + ": " + m;
        return false;
    };
    if (text.size() > kMaxFileBytes) { if (err) *err = "file is too big"; return false; }
    Graph g;
    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    bool header = false;
    while (std::getline(in, raw)) {
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::size_t a = raw.find_first_not_of(" \t");
        if (a == std::string::npos || raw[a] == '#') continue;
        std::vector<std::string> t = Tokens(raw);
        if (!header) {
            int ver = 0;
            if (t.size() != 2 || t[0] != "okaymatgraph") return fail(lineNo, "not a material graph (first line must be 'okaymatgraph 1')");
            if (!ParseInt(t[1], ver)) return fail(lineNo, "bad version number");
            if (ver != 1) return fail(lineNo, "version " + std::to_string(ver) + " is not supported (this build reads version 1)");
            header = true;
            continue;
        }
        if (t[0] == "node") {
            if (t.size() < 3) return fail(lineNo, "node needs an id and a type");
            if ((int)g.nodes.size() >= kMaxNodes) return fail(lineNo, "too many nodes");
            Node n;
            if (!ParseInt(t[1], n.id)) return fail(lineNo, "bad node id '" + t[1] + "'");
            n.type = t[2];
            for (std::size_t i = 3; i < t.size(); ++i) {
                const std::string& k = t[i];
                if (k.rfind("path=", 0) == 0) {
                    std::size_t at = raw.find("path=");
                    n.path = raw.substr(at + 5);
                    break;   // the path runs to the end of the line
                } else if (k.rfind("v=", 0) == 0) {
                    if (!ParseFloats4(k.substr(2), n.value)) return fail(lineNo, "bad v= (need 4 numbers like v=1,0,0,0)");
                } else if (k.rfind("pos=", 0) == 0) {
                    std::string s = k.substr(4); std::size_t c = s.find(',');
                    if (c == std::string::npos || !ParseFloat(s.substr(0, c), n.posX) || !ParseFloat(s.substr(c + 1), n.posY))
                        return fail(lineNo, "bad pos= (need x,y)");
                } else return fail(lineNo, "unknown node field '" + k + "'");
            }
            g.nodes.push_back(n);
        } else if (t[0] == "def") {
            int id = 0; std::string pin; std::array<float, 4> v{};
            if (t.size() != 3 || !SplitRef(t[1], id, pin) || !ParseFloats4(t[2], v.data()))
                return fail(lineNo, "def needs '<node>.<pin> a,b,c,d'");
            Node* n = g.Find(id);
            if (!n) return fail(lineNo, "def names node " + std::to_string(id) + " that is not (yet) defined");
            n->defaults[pin] = v;
        } else if (t[0] == "link") {
            Link l;
            if (t.size() != 3 || !SplitRef(t[1], l.fromNode, l.fromPin) || !SplitRef(t[2], l.toNode, l.toPin))
                return fail(lineNo, "link needs '<node>.<pin> <node>.<pin>'");
            if ((int)g.links.size() >= kMaxLinks) return fail(lineNo, "too many links");
            g.links.push_back(l);
        } else {
            return fail(lineNo, "unknown line '" + t[0] + "'");
        }
    }
    if (!header) { if (err) *err = "empty file (first line must be 'okaymatgraph 1')"; return false; }
    out = std::move(g);
    return true;
}

bool Graph::SaveToFile(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << ToText();
    return (bool)f;
}

bool Graph::LoadFromFile(const std::string& path, Graph& out, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return FromText(text, out, err);
}

// ---------------------------------------------------------------- analysis (validate)

namespace {

struct Analysis {
    std::vector<Issue> issues;
    std::vector<int> order;                          // node indices, sources before users (reachable or not)
    std::map<int, std::array<int, 4>> outW;          // node id -> width of each output
    std::map<int, int> anyW;                         // node id -> width of its Any pins
    std::map<int, const Link*> inLink;               // key = node id * 64 + input index
    bool cyclic = false;
};

void Err(Analysis& a, int node, const std::string& m) { a.issues.push_back({true, node, m}); }
int Key(int node, int pin) { return node * 64 + pin; }

Analysis Analyze(const Graph& g) {
    Analysis a;
    std::map<int, int> index;   // id -> node index
    int outputs = 0;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const Node& n = g.nodes[i];
        if (n.id <= 0) { Err(a, n.id, "node id must be 1 or more"); continue; }
        if (index.count(n.id)) { Err(a, n.id, "two nodes share id " + std::to_string(n.id)); continue; }
        index[n.id] = (int)i;
        const NodeDef* d = FindNodeDef(n.type);
        if (!d) { Err(a, n.id, "unknown node type '" + n.type + "'"); continue; }
        if (n.type == "Output") ++outputs;
    }
    if (outputs == 0) Err(a, 0, "the graph has no Output node");
    if (outputs > 1) Err(a, 0, "the graph has " + std::to_string(outputs) + " Output nodes (exactly one is allowed)");

    std::vector<const Link*> good;
    for (const Link& l : g.links) {
        auto fi = index.find(l.fromNode), ti = index.find(l.toNode);
        if (fi == index.end() || ti == index.end()) {
            Err(a, fi == index.end() ? l.fromNode : l.toNode, "a link names node " +
                std::to_string(fi == index.end() ? l.fromNode : l.toNode) + " which does not exist");
            continue;
        }
        const NodeDef* fd = FindNodeDef(g.nodes[fi->second].type);
        const NodeDef* td = FindNodeDef(g.nodes[ti->second].type);
        if (!fd || !td) continue;   // already reported as unknown type
        if (PinIndex(fd->outputs, l.fromPin) < 0) {
            Err(a, l.fromNode, "node " + std::to_string(l.fromNode) + " (" + fd->type + ") has no output pin '" + l.fromPin + "'");
            continue;
        }
        int ip = PinIndex(td->inputs, l.toPin);
        if (ip < 0) {
            Err(a, l.toNode, "node " + std::to_string(l.toNode) + " (" + td->type + ") has no input pin '" + l.toPin + "'");
            continue;
        }
        if (a.inLink.count(Key(l.toNode, ip))) {
            Err(a, l.toNode, "input '" + l.toPin + "' of node " + std::to_string(l.toNode) + " has two wires");
            continue;
        }
        a.inLink[Key(l.toNode, ip)] = &l;
        good.push_back(&l);
    }

    // Kahn's algorithm: order and cycle check.
    std::map<int, int> indeg;
    std::map<int, std::vector<int>> users;
    for (const auto& kv : index) indeg[kv.first] = 0;
    for (const Link* l : good) { indeg[l->toNode]++; users[l->fromNode].push_back(l->toNode); }
    std::vector<int> ready;
    for (const auto& kv : indeg) if (kv.second == 0) ready.push_back(kv.first);
    std::vector<int> topo;
    while (!ready.empty()) {
        int id = ready.back(); ready.pop_back();
        topo.push_back(id);
        for (int u : users[id]) if (--indeg[u] == 0) ready.push_back(u);
    }
    if (topo.size() != index.size()) {
        a.cyclic = true;
        for (const auto& kv : indeg) if (kv.second > 0) { Err(a, kv.first, "the graph has a cycle through node " + std::to_string(kv.first)); break; }
        return a;
    }
    for (int id : topo) a.order.push_back(index[id]);

    // Widths and type fit, in order.
    for (int idx : a.order) {
        const Node& n = g.nodes[idx];
        const NodeDef* d = FindNodeDef(n.type);
        if (!d) continue;
        int any = 1;
        std::vector<int> srcW(d->inputs.size(), 0);
        for (std::size_t i = 0; i < d->inputs.size(); ++i) {
            auto it = a.inLink.find(Key(n.id, (int)i));
            if (it == a.inLink.end()) continue;
            const Link* l = it->second;
            const NodeDef* fd = FindNodeDef(g.nodes[index[l->fromNode]].type);
            int op = PinIndex(fd->outputs, l->fromPin);
            srcW[i] = a.outW[l->fromNode][op];
            if (d->inputs[i].type == P::Any) any = std::max(any, srcW[i]);
        }
        for (std::size_t i = 0; i < d->inputs.size(); ++i) {
            if (!srcW[i] || d->inputs[i].type == P::Any) continue;
            int k = (int)d->inputs[i].type;
            if (k == 1 ? srcW[i] != 1 : !(srcW[i] == 1 || srcW[i] >= k))
                Err(a, n.id, std::string("cannot wire a ") + (srcW[i] == 1 ? "Float" : srcW[i] == 2 ? "Vec2" : srcW[i] == 3 ? "Vec3" : "Vec4") +
                    " into input '" + d->inputs[i].name + "' of " + d->type + " (it takes a " +
                    (k == 1 ? "Float" : k == 2 ? "Vec2 (or a Float, or something wider)" : k == 3 ? "Vec3 (or a Float, or a Vec4)" : "Vec4") + ")");
        }
        a.anyW[n.id] = any;
        std::array<int, 4> ow{1, 1, 1, 1};
        for (std::size_t o = 0; o < d->outputs.size() && o < 4; ++o) ow[o] = d->outputs[o].type == P::Any ? any : (int)d->outputs[o].type;
        a.outW[n.id] = ow;
    }

    // Warnings: nodes that do not reach the Output.
    int outId = g.OutputNode();
    if (outId && outputs == 1) {
        std::set<int> seen; std::vector<int> stack{outId};
        std::map<int, std::vector<int>> feeders;
        for (const Link* l : good) feeders[l->toNode].push_back(l->fromNode);
        while (!stack.empty()) {
            int id = stack.back(); stack.pop_back();
            if (!seen.insert(id).second) continue;
            for (int f : feeders[id]) stack.push_back(f);
        }
        for (const auto& kv : index) if (!seen.count(kv.first)) a.issues.push_back({false, kv.first, "node " + std::to_string(kv.first) + " does not reach the Output (ignored)"});
    }
    return a;
}

}  // namespace

std::vector<Issue> Validate(const Graph& g) { return Analyze(g).issues; }

bool HasErrors(const std::vector<Issue>& issues) {
    for (const Issue& i : issues) if (i.error) return true;
    return false;
}

// ---------------------------------------------------------------- compile + evaluate

bool Compile(const Graph& g, Program& p) {
    p = Program();
    Analysis a = Analyze(g);
    p.issues = a.issues;
    if (HasErrors(a.issues)) return false;
    // Reachable set from Output.
    std::map<int, std::vector<int>> feeders;
    for (const auto& kv : a.inLink) feeders[kv.second->toNode].push_back(kv.second->fromNode);
    std::set<int> seen; std::vector<int> stack{g.OutputNode()};
    while (!stack.empty()) {
        int id = stack.back(); stack.pop_back();
        if (!seen.insert(id).second) continue;
        for (int f : feeders[id]) stack.push_back(f);
    }
    std::map<int, int> stepOf;
    for (int idx : a.order) {
        const Node& n = g.nodes[idx];
        if (!seen.count(n.id)) continue;
        const NodeDef* d = FindNodeDef(n.type);
        Program::Step s;
        s.node = n.id; s.type = n.type; s.path = n.path;
        for (int i = 0; i < 4; ++i) s.value[i] = n.value[i];
        s.nIn = (int)d->inputs.size();
        s.width = a.anyW[n.id];
        for (int i = 0; i < 4; ++i) s.outN[i] = a.outW[n.id][i];
        for (int i = 0; i < s.nIn; ++i) {
            const PinDef& pd = d->inputs[i];
            auto it = a.inLink.find(Key(n.id, i));
            for (int c = 0; c < 4; ++c) s.def[i][c] = pd.def[c];
            auto ov = n.defaults.find(pd.name);
            if (ov != n.defaults.end()) { for (int c = 0; c < 4; ++c) s.def[i][c] = ov->second[c]; }
            if (it != a.inLink.end()) {
                const Link* l = it->second;
                s.src[i] = stepOf[l->fromNode];
                const Node* fn = g.Find(l->fromNode);
                s.srcPin[i] = PinIndex(FindNodeDef(fn->type)->outputs, l->fromPin);
            } else if (pd.implicit && ov == n.defaults.end()) {
                s.src[i] = pd.implicit == 1 ? -2 : -3; s.srcPin[i] = 0;
            } else { s.src[i] = -1; s.srcPin[i] = 0; }
        }
        stepOf[n.id] = (int)p.steps.size();
        if (n.type == "Output") p.outputStep = (int)p.steps.size();
        p.steps.push_back(s);
    }
    p.m_valid = p.outputStep >= 0;
    return p.m_valid;
}

namespace {

Value Make(int n, float a = 0, float b = 0, float c = 0, float d = 0) {
    Value v; v.n = n; v.v[0] = a; v.v[1] = b; v.v[2] = c; v.v[3] = d; return v;
}
// Widen a single number to n components; wider than n: keep the first n.
Value Fit(const Value& x, int n) {
    Value r; r.n = n;
    for (int i = 0; i < n; ++i) r.v[i] = x.n == 1 ? x.v[0] : (i < x.n ? x.v[i] : 0.0f);
    return r;
}
float Fract(float x) { return x - std::floor(x); }
float Hash2(float x, float y) { return Fract(std::sin(x * 91.3458f + y * 47.8231f) * 28461.1357f); }
float ValueNoise(float px, float py) {
    float ix = std::floor(px), iy = std::floor(py);
    float fx = px - ix, fy = py - iy;
    float ux = fx * fx * (3.0f - 2.0f * fx), uy = fy * fy * (3.0f - 2.0f * fy);
    float a = Hash2(ix, iy), b = Hash2(ix + 1, iy), c = Hash2(ix, iy + 1), d = Hash2(ix + 1, iy + 1);
    float top = a + (b - a) * ux, bot = c + (d - c) * ux;
    return top + (bot - top) * uy;
}
float Sat(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

}  // namespace

Result Run(const Program& p, const EvalContext& ctx) {
    Result res;
    if (!p.valid()) return res;
    std::vector<std::array<Value, 4>> val(p.steps.size());
    for (std::size_t si = 0; si < p.steps.size(); ++si) {
        const Program::Step& s = p.steps[si];
        const NodeDef* d = FindNodeDef(s.type);
        Value in[8];
        for (int i = 0; i < s.nIn; ++i) {
            const PinDef& pd = d->inputs[i];
            Value v;
            if (s.src[i] >= 0) v = val[s.src[i]][s.srcPin[i]];
            else if (s.src[i] == -2) v = Make(2, ctx.uv.x, ctx.uv.y);
            else if (s.src[i] == -3) v = Make(3, ctx.normal.x, ctx.normal.y, ctx.normal.z);
            else v = pd.type == P::Any ? Make(1, s.def[i][0]) : Make((int)pd.type, s.def[i][0], s.def[i][1], s.def[i][2], s.def[i][3]);
            in[i] = pd.type == P::Any ? Fit(v, s.width) : Fit(v, (int)pd.type);
        }
        std::array<Value, 4>& out = val[si];
        const int W = s.width;
        auto each = [&](auto fn) { Value r; r.n = W; for (int c = 0; c < W; ++c) r.v[c] = fn(c); return r; };
        const std::string& t = s.type;
        if (t == "Float") out[0] = Make(1, s.value[0]);
        else if (t == "Vec2") out[0] = Make(2, s.value[0], s.value[1]);
        else if (t == "Vec3") out[0] = Make(3, s.value[0], s.value[1], s.value[2]);
        else if (t == "UV") out[0] = Make(2, ctx.uv.x, ctx.uv.y);
        else if (t == "Time") out[0] = Make(1, ctx.time);
        else if (t == "Normal") out[0] = Make(3, ctx.normal.x, ctx.normal.y, ctx.normal.z);
        else if (t == "ViewDir") out[0] = Make(3, ctx.viewDir.x, ctx.viewDir.y, ctx.viewDir.z);
        else if (t == "TextureSample" || t == "NormalMap") {
            float px[4] = {1, 1, 1, 1};
            if (t == "NormalMap") { px[0] = 0.5f; px[1] = 0.5f; px[2] = 1.0f; px[3] = 1.0f; }
            if (ctx.sampler) ctx.sampler(s.path, in[0].v[0], in[0].v[1], px);
            if (t == "TextureSample") {
                out[0] = Make(4, px[0], px[1], px[2], px[3]);
                out[1] = Make(3, px[0], px[1], px[2]);
                out[2] = Make(1, px[3]);
            } else {
                float x = (px[0] * 2 - 1) * in[1].v[0], y = (px[1] * 2 - 1) * in[1].v[0], z = px[2] * 2 - 1;
                Vec3 nn = Vec3(x, y, z).Normalized();
                out[0] = Make(3, nn.x, nn.y, nn.z);
            }
        }
        else if (t == "Add") out[0] = each([&](int c) { return in[0].v[c] + in[1].v[c]; });
        else if (t == "Subtract") out[0] = each([&](int c) { return in[0].v[c] - in[1].v[c]; });
        else if (t == "Multiply") out[0] = each([&](int c) { return in[0].v[c] * in[1].v[c]; });
        else if (t == "Divide") out[0] = each([&](int c) { return in[1].v[c] == 0.0f ? 0.0f : in[0].v[c] / in[1].v[c]; });
        else if (t == "Lerp") out[0] = each([&](int c) { return in[0].v[c] + (in[1].v[c] - in[0].v[c]) * in[2].v[c]; });
        else if (t == "Clamp") out[0] = each([&](int c) { return std::min(std::max(in[0].v[c], in[1].v[c]), in[2].v[c]); });
        else if (t == "Saturate") out[0] = each([&](int c) { return Sat(in[0].v[c]); });
        else if (t == "Pow") out[0] = each([&](int c) { return std::pow(std::max(in[0].v[c], 0.0f), in[1].v[c]); });
        else if (t == "Dot") { float sum = 0; for (int c = 0; c < W; ++c) sum += in[0].v[c] * in[1].v[c]; out[0] = Make(1, sum); }
        else if (t == "Normalize") {
            float len = 0; for (int c = 0; c < W; ++c) len += in[0].v[c] * in[0].v[c];
            len = std::sqrt(len);
            out[0] = each([&](int c) { return len > 1e-12f ? in[0].v[c] / len : 0.0f; });
        }
        else if (t == "OneMinus") out[0] = each([&](int c) { return 1.0f - in[0].v[c]; });
        else if (t == "Sin") out[0] = each([&](int c) { return std::sin(in[0].v[c]); });
        else if (t == "MakeVec3") out[0] = Make(3, in[0].v[0], in[1].v[0], in[2].v[0]);
        else if (t == "Split") for (int c = 0; c < 4; ++c) out[c] = Make(1, c < W ? in[0].v[c] : 0.0f);
        else if (t == "Fresnel") {
            float nv = in[0].v[0] * ctx.viewDir.x + in[0].v[1] * ctx.viewDir.y + in[0].v[2] * ctx.viewDir.z;
            out[0] = Make(1, std::pow(1.0f - Sat(nv), in[1].v[0]));
        }
        else if (t == "Noise") out[0] = Make(1, ValueNoise(in[0].v[0] * in[1].v[0], in[0].v[1] * in[1].v[0]));
        else if (t == "Desaturate") {
            float g = in[0].v[0] * 0.2126f + in[0].v[1] * 0.7152f + in[0].v[2] * 0.0722f;
            out[0] = Make(3, in[0].v[0] + (g - in[0].v[0]) * in[1].v[0], in[0].v[1] + (g - in[0].v[1]) * in[1].v[0],
                          in[0].v[2] + (g - in[0].v[2]) * in[1].v[0]);
        }
        else if (t == "Contrast") out[0] = each([&](int c) { return (in[0].v[c] - 0.5f) * in[1].v[c] + 0.5f; });
        else if (t == "Output") {
            res.baseColor = {in[0].v[0], in[0].v[1], in[0].v[2]};
            res.emission = {in[1].v[0], in[1].v[1], in[1].v[2]};
            res.roughness = in[2].v[0]; res.metallic = in[3].v[0]; res.alpha = in[4].v[0];
            res.normal = {in[5].v[0], in[5].v[1], in[5].v[2]};
        }
    }
    return res;
}

bool Evaluate(const Graph& g, const EvalContext& ctx, Result& out, std::string* err) {
    Program p;
    if (!Compile(g, p)) {
        if (err) for (const Issue& i : p.issues) if (i.error) { *err = i.message; break; }
        return false;
    }
    out = Run(p, ctx);
    return true;
}

// ---------------------------------------------------------------- software preview

Image RenderPreviewSphere(const Program& p, int size, float time, const TextureSampler& sampler) {
    if (size < 2) size = 2;
    Image img(size, size);
    const float PI = 3.14159265358979f;
    const Vec3 L = Vec3(-0.5f, 0.6f, 0.62f).Normalized();
    const Vec3 V(0, 0, 1);
    const Vec3 H = (L + V).Normalized();
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            float x = ((px + 0.5f) / size) * 2.0f - 1.0f, y = 1.0f - ((py + 0.5f) / size) * 2.0f;
            float r2 = x * x + y * y;
            if (r2 > 1.0f) continue;
            Vec3 N(x, y, std::sqrt(1.0f - r2));
            EvalContext ctx;
            ctx.uv = {std::atan2(N.x, N.z) / (2.0f * PI) + 0.5f, 0.5f - std::asin(std::max(-1.0f, std::min(1.0f, N.y))) / PI};
            ctx.normal = N; ctx.viewDir = V; ctx.time = time; ctx.sampler = sampler;
            Result m = Run(p, ctx);
            // Tangent frame: T along increasing u, B across; tangent-space normal to surface space.
            Vec3 T(N.z, 0, -N.x);
            T = T.SqrMagnitude() < 1e-6f ? Vec3(1, 0, 0) : T.Normalized();
            Vec3 B = Vec3::Cross(N, T);
            Vec3 SN = (T * m.normal.x + B * m.normal.y + N * m.normal.z).Normalized();
            if (SN.SqrMagnitude() < 0.5f) SN = N;
            float rough = Sat(m.roughness), metal = Sat(m.metallic);
            float diff = std::max(Vec3::Dot(SN, L), 0.0f);
            float shin = 4.0f + (1.0f - rough) * (1.0f - rough) * 252.0f;
            float spec = std::pow(std::max(Vec3::Dot(SN, H), 0.0f), shin) * (1.0f - 0.5f * rough) * (diff > 0 ? 1.0f : 0.0f);
            float c[3] = {m.baseColor.x, m.baseColor.y, m.baseColor.z};
            float e[3] = {m.emission.x, m.emission.y, m.emission.z};
            Color out(0, 0, 0, Sat(m.alpha));
            float* o[3] = {&out.r, &out.g, &out.b};
            for (int k = 0; k < 3; ++k) {
                float f0 = 0.04f + (c[k] - 0.04f) * metal;
                float lin = c[k] * (1.0f - metal) * (0.15f + diff) + f0 * spec + e[k];
                *o[k] = std::pow(Sat(lin), 1.0f / 2.2f);
            }
            img.SetPixel(px, py, out);
        }
    }
    return img;
}

// ---------------------------------------------------------------- shader text

namespace {

struct Lang {
    bool hlsl;
    std::string type(int n) const { return n == 1 ? "float" : (hlsl ? "float" + std::to_string(n) : "vec" + std::to_string(n)); }
    const char* mix() const { return hlsl ? "lerp" : "mix"; }
    const char* fract() const { return hlsl ? "frac" : "fract"; }
};

std::string Lit(float f) {
    char b[48]; std::snprintf(b, sizeof(b), "%.6g", (double)f);
    for (char* q = b; *q; ++q) if (*q == ',') *q = '.';
    std::string s = b;
    if (s.find_first_of(".en") == std::string::npos) s += ".0";   // "1" -> "1.0"; leaves 1e-05, inf, nan alone
    return s;
}
std::string VecLit(const Lang& L, const float* v, int n) {
    if (n == 1) return Lit(v[0]);
    std::string s = L.type(n) + "(";
    for (int i = 0; i < n; ++i) s += (i ? ", " : "") + Lit(v[i]);
    return s + ")";
}
// Make expression `e` of width `from` into width `to` (floats widen, wider values lose their tail).
std::string Coerce(const Lang& L, const std::string& e, int from, int to) {
    if (from == to) return e;
    static const char* sw[] = {"", "x", "xy", "xyz", "xyzw"};
    if (from == 1) {
        if (!L.hlsl) return L.type(to) + "(" + e + ")";
        std::string s = L.type(to) + "(";
        for (int i = 0; i < to; ++i) s += (i ? ", " : "") + e;
        return s + ")";
    }
    return e + "." + sw[to];
}
std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

}  // namespace

std::string EmitShader(const Graph& g, Target target, std::string* err) {
    Program prog;
    Analysis an = Analyze(g);
    if (HasErrors(an.issues)) {
        if (err) for (const Issue& i : an.issues) if (i.error) { *err = i.message; break; }
        return "";
    }
    Compile(g, prog);
    const Lang L{target == Target::Hlsl};
    bool usesNoise = false;
    std::ostringstream hs, bs;
    // variable name of output o of step si
    auto var = [&](int si, int o) {
        const Program::Step& s = prog.steps[si];
        return "n" + std::to_string(s.node) + "_" + Lower(FindNodeDef(s.type)->outputs[o].name);
    };
    hs << (L.hlsl ? "// okaymatgraph -> HLSL\n" : "// okaymatgraph -> GLSL 120\n");
    for (std::size_t si = 0; si < prog.steps.size(); ++si) {
        const Program::Step& s = prog.steps[si];
        const NodeDef* d = FindNodeDef(s.type);
        const int W = s.width;
        std::string in[8];
        for (int i = 0; i < s.nIn; ++i) {
            const PinDef& pd = d->inputs[i];
            int want = pd.type == P::Any ? W : (int)pd.type;
            if (s.src[i] >= 0) {
                in[i] = Coerce(L, var(s.src[i], s.srcPin[i]), prog.steps[s.src[i]].outN[s.srcPin[i]], want);
            } else if (s.src[i] == -2) in[i] = Coerce(L, "uv", 2, want);
            else if (s.src[i] == -3) in[i] = Coerce(L, "n_surface", 3, want);
            else in[i] = VecLit(L, s.def[i], want);
        }
        const std::string tex = "okay_tex_" + std::to_string(s.node);
        auto decl = [&](int o, const std::string& expr) {
            bs << "    " << L.type(s.outN[o]) << " " << var((int)si, o) << " = " << expr << ";\n";
        };
        const std::string& t = s.type;
        const char* mix = L.mix();
        if (t == "Float") decl(0, VecLit(L, s.value, 1));
        else if (t == "Vec2") decl(0, VecLit(L, s.value, 2));
        else if (t == "Vec3") decl(0, VecLit(L, s.value, 3));
        else if (t == "UV") decl(0, "uv");
        else if (t == "Time") decl(0, "time");
        else if (t == "Normal") decl(0, "n_surface");
        else if (t == "ViewDir") decl(0, "view");
        else if (t == "TextureSample" || t == "NormalMap") {
            hs << (L.hlsl ? "Texture2D " : "uniform sampler2D ") << tex << ";   // " << (s.path.empty() ? "(no path set)" : s.path) << "\n";
            std::string fetch = L.hlsl ? tex + ".Sample(okay_samp, " + in[0] + ")" : "texture2D(" + tex + ", " + in[0] + ")";
            if (t == "TextureSample") {
                decl(0, fetch);
                decl(1, var((int)si, 0) + ".xyz");
                decl(2, var((int)si, 0) + ".w");
            } else {
                bs << "    " << L.type(3) << " " << var((int)si, 0) << "_t = " << fetch << ".xyz;\n";
                decl(0, "normalize(" + L.type(3) + "((" + var((int)si, 0) + "_t.xy * 2.0 - 1.0) * " + in[1] + ", " + var((int)si, 0) + "_t.z * 2.0 - 1.0))");
            }
        }
        else if (t == "Add") decl(0, "(" + in[0] + " + " + in[1] + ")");
        else if (t == "Subtract") decl(0, "(" + in[0] + " - " + in[1] + ")");
        else if (t == "Multiply") decl(0, "(" + in[0] + " * " + in[1] + ")");
        else if (t == "Divide") decl(0, "(" + in[0] + " / " + in[1] + ")");
        else if (t == "Lerp") decl(0, std::string(mix) + "(" + in[0] + ", " + in[1] + ", " + in[2] + ")");
        else if (t == "Clamp") decl(0, "clamp(" + in[0] + ", " + in[1] + ", " + in[2] + ")");
        else if (t == "Saturate") decl(0, L.hlsl ? "saturate(" + in[0] + ")" : "clamp(" + in[0] + ", 0.0, 1.0)");
        else if (t == "Pow") decl(0, "pow(max(" + in[0] + ", " + Coerce(L, "0.0", 1, W) + "), " + in[1] + ")");
        else if (t == "Dot") decl(0, W == 1 ? "(" + in[0] + " * " + in[1] + ")" : "dot(" + in[0] + ", " + in[1] + ")");
        else if (t == "Normalize") decl(0, W == 1 ? std::string(L.hlsl ? "(float)sign(" : "sign(") + in[0] + ")" : "normalize(" + in[0] + ")");
        else if (t == "OneMinus") decl(0, "(1.0 - " + in[0] + ")");
        else if (t == "Sin") decl(0, "sin(" + in[0] + ")");
        else if (t == "MakeVec3") decl(0, L.type(3) + "(" + in[0] + ", " + in[1] + ", " + in[2] + ")");
        else if (t == "Split") {
            static const char* comp[] = {"x", "y", "z", "w"};
            for (int c = 0; c < 4; ++c) decl(c, c >= W ? "0.0" : W == 1 ? in[0] : in[0] + "." + comp[c]);
        }
        else if (t == "Fresnel") decl(0, "pow(1.0 - " + std::string(L.hlsl ? "saturate(" : "clamp(") + "dot(" + in[0] + ", view)" +
                                         (L.hlsl ? ")" : ", 0.0, 1.0)") + ", " + in[1] + ")");
        else if (t == "Noise") { usesNoise = true; decl(0, "okay_noise(" + in[0] + " * " + in[1] + ")"); }
        else if (t == "Desaturate")
            decl(0, std::string(mix) + "(" + in[0] + ", " + L.type(3) + "(dot(" + in[0] + ", " + L.type(3) + "(0.2126, 0.7152, 0.0722))), " + in[1] + ")");
        else if (t == "Contrast") decl(0, "((" + in[0] + " - 0.5) * " + in[1] + " + 0.5)");
        else if (t == "Output") {
            bs << "    o_base_color = " << in[0] << ";\n    o_emission = " << in[1] << ";\n    o_roughness = " << in[2]
               << ";\n    o_metallic = " << in[3] << ";\n    o_alpha = " << in[4] << ";\n    o_normal = " << in[5] << ";\n";
        }
    }
    std::ostringstream o;
    o << hs.str();
    // One shared sampler for every HLSL texture, declared once after the texture lines.
    if (L.hlsl && hs.str().find("Texture2D") != std::string::npos) o << "SamplerState okay_samp;\n";
    if (usesNoise) {
        const std::string f2 = L.type(2);
        o << "float okay_hash(" << f2 << " c) { return " << L.fract() << "(sin(dot(c, " << f2 << "(91.3458, 47.8231))) * 28461.1357); }\n"
          << "float okay_noise(" << f2 << " p) {\n"
          << "    " << f2 << " i = floor(p);\n"
          << "    " << f2 << " f = p - i;\n"
          << "    " << f2 << " u = f * f * (3.0 - 2.0 * f);\n"
          << "    float a = okay_hash(i);\n"
          << "    float b = okay_hash(i + " << f2 << "(1.0, 0.0));\n"
          << "    float c = okay_hash(i + " << f2 << "(0.0, 1.0));\n"
          << "    float d = okay_hash(i + " << f2 << "(1.0, 1.0));\n"
          << "    return " << L.mix() << "(" << L.mix() << "(a, b, u.x), " << L.mix() << "(c, d, u.x), u.y);\n"
          << "}\n";
    }
    o << "void okay_material(" << L.type(2) << " uv, " << L.type(3) << " n_surface, " << L.type(3) << " view, float time,\n"
      << "                   out " << L.type(3) << " o_base_color, out " << L.type(3) << " o_emission, out float o_roughness,\n"
      << "                   out float o_metallic, out float o_alpha, out " << L.type(3) << " o_normal)\n{\n"
      << bs.str() << "}\n";
    return o.str();
}

// ---------------------------------------------------------------- example

Graph ExampleGraph() {
    Graph g;
    int out = g.Add("Output", 900, 100);
    int noise = g.Add("Noise", 0, 0);
    g.SetDefault(noise, "Scale", 6);
    int a = g.AddValue("Vec3", 0.55f, 0.18f, 0.12f);
    int b = g.AddValue("Vec3", 0.85f, 0.62f, 0.45f);
    g.Find(a)->posX = 0; g.Find(a)->posY = 160; g.Find(b)->posX = 0; g.Find(b)->posY = 260;
    int mix = g.Add("Lerp", 250, 100);
    g.Connect(a, "Value", mix, "A"); g.Connect(b, "Value", mix, "B"); g.Connect(noise, "Result", mix, "T");
    g.Connect(mix, "Result", out, "BaseColor");
    int rough = g.Add("Lerp", 250, 300);
    g.SetDefault(rough, "A", 0.3f); g.SetDefault(rough, "B", 0.9f);
    g.Connect(noise, "Result", rough, "T");
    g.Connect(rough, "Result", out, "Roughness");
    int fres = g.Add("Fresnel", 250, 450);
    g.SetDefault(fres, "Power", 3);
    int rim = g.AddValue("Vec3", 0.2f, 0.45f, 1.0f);
    g.Find(rim)->posX = 250; g.Find(rim)->posY = 550;
    int tint = g.Add("Multiply", 500, 450);
    g.Connect(fres, "Result", tint, "A"); g.Connect(rim, "Value", tint, "B");
    int time = g.Add("Time", 250, 650);
    int sn = g.Add("Sin", 400, 650);
    g.Connect(time, "Time", sn, "In");
    int sc = g.Add("Multiply", 550, 650);
    g.Connect(sn, "Result", sc, "A"); g.SetDefault(sc, "B", 0.25f);
    int off = g.Add("Add", 700, 650);
    g.Connect(sc, "Result", off, "A"); g.SetDefault(off, "B", 0.75f);
    int pulse = g.Add("Multiply", 700, 450);
    g.Connect(tint, "Result", pulse, "A"); g.Connect(off, "Result", pulse, "B");
    g.Connect(pulse, "Result", out, "Emission");
    return g;
}

}  // namespace matgraph
}  // namespace okay
