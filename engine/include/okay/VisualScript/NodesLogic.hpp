#pragma once
// ---------------------------------------------------------------------------
// Logic the graph runtime was missing: maps, named and numeric switches,
// a stateful gate, For Each over a map (with Break), calling a text-script
// function, and running another graph as a subgraph. Loops that reach a
// Delay park themselves; these nodes follow the same rule.
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/Nodes.hpp"
#include "okay/Components/ScriptComponent.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace okay::vs {

inline std::string VsTrim(std::string s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

inline std::vector<std::string> VsSplitCases(const std::string& arg) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : arg) {
        if (c == ',') { out.push_back(VsTrim(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty() || (!arg.empty() && arg.back() == ',')) out.push_back(VsTrim(cur));
    out.erase(std::remove_if(out.begin(), out.end(), [](const std::string& s) { return s.empty(); }), out.end());
    return out;
}

// ---- Maps ------------------------------------------------------------------
inline VsValue::MapRef VsMapIn(VsNode* n, const VsValue& v, NodeGraph& g, VsContext& ctx, bool required) {
    auto m = v.AsMap();
    if (!m && required) g.Error(n, ctx, "its Map input isn't a map (wire in New Map, or a variable holding one)");
    return m;
}

struct NewMapNode : VsNode {
    VsValue Eval(int, NodeGraph&, VsContext&) override { return VsValue::MakeMap(); }
};

struct MapSetNode : VsNode {   // in0 = map, in1 = key, in2 = value
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (auto m = VsMapIn(this, In(0, g, ctx), g, ctx, true))
            (*m)[In(1, g, ctx).AsString()] = In(2, g, ctx);
        return 0;
    }
};

struct MapRemoveNode : VsNode {   // in0 = map, in1 = key
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (auto m = VsMapIn(this, In(0, g, ctx), g, ctx, true))
            m->erase(In(1, g, ctx).AsString());
        return 0;
    }
};

struct MapGetNode : VsNode {   // in0 = map, in1 = key; out0 = value, out1 = found
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        auto m = VsMapIn(this, In(0, g, ctx), g, ctx, HasInput(0));
        if (!m) return pin == 1 ? VsValue{false} : VsValue{};
        auto it = m->find(In(1, g, ctx).AsString());
        if (it == m->end()) return pin == 1 ? VsValue{false} : VsValue{};
        return pin == 1 ? VsValue{true} : it->second;
    }
};

struct MapHasNode : VsNode {   // in0 = map, in1 = key
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        auto m = VsMapIn(this, In(0, g, ctx), g, ctx, HasInput(0));
        if (!m) return false;
        return m->find(In(1, g, ctx).AsString()) != m->end();
    }
};

struct MapKeysNode : VsNode {   // in0 = map; out0 = list of keys (sorted)
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        VsValue out = VsValue::MakeArray();
        auto m = VsMapIn(this, In(0, g, ctx), g, ctx, HasInput(0));
        if (!m) return out;
        auto a = out.AsArray();
        for (const auto& kv : *m) a->push_back(VsValue(kv.first));
        return out;
    }
};

struct MapValuesNode : VsNode {   // in0 = map; out0 = list of values (key order)
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        VsValue out = VsValue::MakeArray();
        auto m = VsMapIn(this, In(0, g, ctx), g, ctx, HasInput(0));
        if (!m) return out;
        auto a = out.AsArray();
        for (const auto& kv : *m) a->push_back(kv.second);
        return out;
    }
};

// For Each Map: Body once per entry (keys in sorted order), Key and Value set,
// then Done. Break / Continue work. A Delay in the body pauses the loop.
struct ForEachMapNode : VsNode {
    std::string key;
    VsValue value;
    int index = -1;
    VsLoopLatch latch;
    std::vector<std::string> keys;
    std::vector<VsValue> vals;
    int ExecOutCount() const override { return 2; }
    VsValue Eval(int pin, NodeGraph&, VsContext&) override { return pin == 0 ? VsValue(key) : value; }
    void OnSuspendRejected() override { latch.Finish(); }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!latch.busy) return false;
        latch.Save(r);
        r.i0 = index;
        r.texts = keys;
        r.values = vals;
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph& g) override {
        latch.Load(r, g);
        index = r.i0;
        keys = r.texts;
        vals = r.values;
        if (index >= 0 && index < (int)keys.size()) {
            key = keys[(std::size_t)index];
            value = vals[(std::size_t)index];
        }
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!latch.Begin(g)) return -1;
        const int body = Next(0);
        if (!g.Resuming()) {
            keys.clear();
            vals.clear();
            const VsValue src = In(0, g, ctx);
            auto m = src.AsMap();
            if (!m) {
                if (HasInput(0)) g.Error(this, ctx, src.IsArray()
                    ? std::string("its Map input is a list (use For Each for lists)")
                    : std::string("its Map input isn't a map (wire in New Map, or a variable holding one)"));
                latch.Finish();
                return 1;
            }
            keys.reserve(m->size());
            vals.reserve(m->size());
            for (const auto& kv : *m) { keys.push_back(kv.first); vals.push_back(kv.second); }
            index = keys.empty() ? -1 : 0;
        }
        g.EnterLoop();
        while (body >= 0 && index >= 0 && index < (int)keys.size() && !g.Aborted()) {
            key = keys[(std::size_t)index];
            value = vals[(std::size_t)index];
            if (latch.Step(g, ctx, body) == NodeGraph::kSuspend) { g.LeaveLoop(); return NodeGraph::kSuspend; }
            if (g.TakeLoopSignal() == NodeGraph::kLoopBreak || g.Aborted()) break;
            if (index + 1 >= (int)keys.size()) break;
            ++index;
        }
        latch.Finish();
        g.LeaveLoop();
        return 1;
    }
};

// ---- Switches --------------------------------------------------------------
// One exec pin per case in the text field, then Default. Any number of cases
// up to the pin limit. An old graph that wired Default to pin 4 (the previous
// fixed layout) is moved onto the real Default pin when it is loaded.
constexpr int kLegacySwitchDefault = 4;

inline int SwitchPinCount(int ncases) {
    if (ncases < 0) ncases = 0;
    if (ncases > NodeGraph::kMaxPins - 1) ncases = NodeGraph::kMaxPins - 1;
    return ncases + 1;
}

struct SwitchStringNode : VsNode {
    std::vector<std::string> cases;
    explicit SwitchStringNode(std::string arg) : cases(VsSplitCases(arg)) {}
    int CaseCount() const { return (int)cases.size(); }
    int ExecOutCount() const override { return SwitchPinCount(CaseCount()); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        int n = CaseCount();
        if (n > NodeGraph::kMaxPins - 1) {
            g.Error(this, ctx, "only the first " + std::to_string(NodeGraph::kMaxPins - 1) + " cases are used");
            n = NodeGraph::kMaxPins - 1;
        }
        const std::string v = In(0, g, ctx).AsString();
        for (int i = 0; i < n; ++i) if (cases[(std::size_t)i] == v) return i;
        return n;
    }
};

struct SwitchCasesNode : VsNode {
    std::vector<int> cases;
    std::vector<char> live;   // 0 = the text wasn't a number, so that pin never matches
    explicit SwitchCasesNode(std::string arg) {
        for (const std::string& s : VsSplitCases(arg)) {
            float f = 0.0f;
            if (VsParseFinite(s, f)) { cases.push_back((int)std::lround(f)); live.push_back(1); }
            else { cases.push_back(0); live.push_back(0); }
        }
    }
    int CaseCount() const { return (int)cases.size(); }
    int ExecOutCount() const override { return SwitchPinCount(CaseCount()); }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        int n = CaseCount();
        if (n > NodeGraph::kMaxPins - 1) {
            g.Error(this, ctx, "only the first " + std::to_string(NodeGraph::kMaxPins - 1) + " cases are used");
            n = NodeGraph::kMaxPins - 1;
        }
        const int v = (int)std::lround(In(0, g, ctx).AsFloat());
        for (int i = 0; i < n; ++i)
            if (live[(std::size_t)i] && cases[(std::size_t)i] == v) return i;
        return n;
    }
};

// Stateful gate. Starts closed (or open when the text field is "open").
// Open / Close / Toggle are bool pulses applied in that order, then the flow
// passes only while it is open. Is Open reads the latch.
struct LatchGateNode : VsNode {
    bool open;
    explicit LatchGateNode(std::string mode) : open(mode == "open") {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return open; }
    bool SaveFlow(VsFlowRecord& r) const override { r.b0 = open; return true; }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override { open = r.b0; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (In(0, g, ctx, VsValue{false}).AsBool()) open = true;
        if (In(1, g, ctx, VsValue{false}).AsBool()) open = false;
        if (In(2, g, ctx, VsValue{false}).AsBool()) open = !open;
        return open ? 0 : -1;
    }
};

// True for a live object, a name that finds one, a finite number, a bool, a
// vector, or a list / map. False for a destroyed object, an empty name, or NaN.
struct IsValidNode : VsNode {
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const VsValue v = In(0, g, ctx);
        if (!HasInput(0)) return false;
        if (v.IsObject()) return VsObjectAlive(v.AsObjectId());
        if (v.IsString()) {
            if (v.AsString().empty()) return false;
            return VsResolveObject(ctx, v) != nullptr;
        }
        if (v.IsNumber()) return std::isfinite(v.AsFloat());
        if (v.IsArray()) return v.AsArray() != nullptr;
        if (v.IsMap()) return v.AsMap() != nullptr;
        return true;
    }
};

// Call a function on this object's text script. in0 is the argument, out0 the
// return value (empty when the function returns nothing).
struct CallScriptNode : VsNode {
    std::string name;
    VsValue last;
    explicit CallScriptNode(std::string n) : name(std::move(n)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return last; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (name.empty()) { g.Error(this, ctx, "no function name to call"); return 0; }
        if (!ctx.gameObject) { g.Error(this, ctx, "there's no object to call '" + name + "' on"); return 0; }
        ScriptComponent* sc = ctx.gameObject->GetComponent<ScriptComponent>();
        if (!sc || !sc->VM()) {
            g.Error(this, ctx, "this object has no text script, so '" + name + "' can't be called");
            return 0;
        }
        if (!sc->VM()->HasEvent(name)) {
            g.Error(this, ctx, "the text script has no function named '" + name + "'");
            return 0;
        }
        std::string err;
        last = sc->VM()->CallEventArgs(name, {In(0, g, ctx)}, &err);
        if (!err.empty()) g.Error(this, ctx, "calling '" + name + "' failed: " + err);
        return 0;
    }
};

// Run another graph's event (default OnStart) on this same object, sharing
// variables. The text field is "path" or "path|Event". A Delay inside the
// called graph pauses this node and continues next frame. Relative paths only.
struct RunGraphNode : VsNode {
    std::string spec;
    std::string path, entry = "OnStart";
    std::unique_ptr<NodeGraph> sub;
    std::string loadedText;
    bool hold = false;
    explicit RunGraphNode(std::string s) : spec(std::move(s)) {
        const auto bar = spec.find('|');
        path = bar == std::string::npos ? spec : spec.substr(0, bar);
        if (bar != std::string::npos && bar + 1 < spec.size()) entry = spec.substr(bar + 1);
        path = VsTrim(path);
        entry = VsTrim(entry);
        if (entry.empty()) entry = "OnStart";
    }
    int ExecOutCount() const override { return 1; }
    void OnSuspendRejected() override {
        hold = false;
        if (sub) sub->CancelSuspended();
    }
    bool SaveFlow(VsFlowRecord& r) const override {
        if (!hold || !sub) return false;
        r.b0 = true;
        r.text = loadedText;
        r.texts = {entry, path};
        sub->CaptureFlow(r.children, r.parked);
        return true;
    }
    void LoadFlow(const VsFlowRecord& r, NodeGraph&) override {
        hold = r.b0;
        if (!r.texts.empty()) entry = r.texts[0];
        if (r.texts.size() > 1) path = r.texts[1];
        if (!hold || r.text.empty()) return;
        std::string err;
        auto parsed = NodeGraph::Parse(r.text, &err);
        if (!parsed) { hold = false; return; }
        parsed->debugKey = path;
        parsed->RestoreFlow(r.children, r.parked);
        sub = std::move(parsed);
        loadedText = r.text;
    }
    bool Unsafe(const std::string& p) const {
        if (p.empty()) return true;
        if (p[0] == '/' || p[0] == '\\') return true;
        if (p.size() >= 2 && p[1] == ':') return true;
        return p.find("..") != std::string::npos;
    }
    std::string Resolve(NodeGraph& g) const {
        auto exists = [](const std::string& p) { std::ifstream f(p); return f.good(); };
        if (exists(path)) return path;
        const auto slash = g.debugKey.find_last_of("/\\");
        if (slash != std::string::npos) {
            const std::string beside = g.debugKey.substr(0, slash + 1) + path;
            if (exists(beside)) return beside;
        }
        return {};
    }
    bool Ensure(NodeGraph& g, VsContext& ctx) {
        if (Unsafe(path)) {
            g.Error(this, ctx, path.empty() ? std::string("no graph file to run")
                                           : "refusing to load '" + path + "' (use a relative path, with no '..')");
            return false;
        }
        const std::string full = Resolve(g);
        if (full.empty()) { g.Error(this, ctx, "couldn't find graph '" + path + "'"); return false; }
        std::ifstream in(full);
        std::stringstream ss; ss << in.rdbuf();
        const std::string text = ss.str();
        if (sub && text == loadedText) return true;
        std::string err;
        auto parsed = NodeGraph::Parse(text, &err);
        if (!parsed) { g.Error(this, ctx, "couldn't load '" + path + "': " + err); return false; }
        parsed->debugKey = full;
        sub = std::move(parsed);
        loadedText = text;
        hold = false;
        return true;
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (!g.Resuming()) {
            if (hold) return -1;
            if (!Ensure(g, ctx)) return 0;
            if (sub->Entries(entry).empty()) {
                g.Error(this, ctx, "graph '" + path + "' has no '" + entry + "' event to run");
                return 0;
            }
            sub->RunEntries(entry, ctx);
            if (sub->HasSuspended()) { hold = true; return NodeGraph::kSuspend; }
            return 0;
        }
        if (!sub && !Ensure(g, ctx)) { hold = false; return 0; }
        if (sub) sub->Resume(ctx);
        if (sub && sub->HasSuspended()) return NodeGraph::kSuspend;
        hold = false;
        return 0;
    }
};

// ---- More map nodes, and structs ----------------------------------------------
struct MapClearNode : VsNode {
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        if (auto m = VsMapIn(this, In(0, g, ctx), g, ctx, true)) m->clear();
        return 0;
    }
};

struct MapCountNode : VsNode {   // in0 = map -> how many entries
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        auto m = In(0, g, ctx).AsMap();
        return m ? (float)m->size() : 0.0f;
    }
};

struct MapCopyNode : VsNode {   // in0 = map, in1 = deep? -> a new map
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        auto m = VsMapIn(this, In(0, g, ctx), g, ctx, true);
        if (!m) return VsValue::MakeMap();
        if (In(1, g, ctx).AsBool()) return VsDeepCopy(VsValue(m));
        return VsValue(std::make_shared<VsValue::Map>(*m));
    }
};

// ---- Structs: a map with declared, typed fields (`struct` line) ----------------
struct StructNewNode : VsNode {   // text = struct name -> a new struct with every field at its default
    std::string name;
    explicit StructNewNode(std::string n) : name(std::move(n)) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        const StructDef* sd = FindStruct(g.Structs(), name);
        if (!sd) { g.Error(this, ctx, "there is no struct named '" + name + "' (declare it in the Variables panel)"); return VsValue::MakeMap(); }
        VarType t; t.base = PinType::Map; t.structName = name;
        return VsDefaultFor(t, g.Structs());
    }
};

struct StructGetNode : VsNode {   // text = field; in0 = struct -> the field's value
    std::string field;
    explicit StructGetNode(std::string f) : field(std::move(f)) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        auto m = VsMapIn(this, In(0, g, ctx), g, ctx, true);
        if (!m) return {};
        auto it = m->find(field);
        if (it == m->end()) { g.Error(this, ctx, "the struct has no field '" + field + "'"); return {}; }
        return it->second;
    }
};

struct StructSetNode : VsNode {   // text = field; in0 = struct, in1 = value
    std::string field;
    explicit StructSetNode(std::string f) : field(std::move(f)) {}
    int ExecOutCount() const override { return 1; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        auto m = VsMapIn(this, In(0, g, ctx), g, ctx, true);
        if (!m) return 0;
        auto it = m->find(field);
        if (it == m->end()) { g.Error(this, ctx, "the struct has no field '" + field + "'; nothing was changed"); return 0; }
        VsValue v = In(1, g, ctx);
        // The field's declared type, found through whichever struct declares all of this map's fields.
        for (const StructDef& sd : g.Structs()) {
            if (sd.fields.size() != m->size()) continue;
            bool same = true;
            for (const auto& f : sd.fields) if (!m->count(f.name)) { same = false; break; }
            if (!same) continue;
            for (const auto& f : sd.fields) if (f.name == field) {
                std::string err;
                if (!VsCoerce(f.type, v, g.Structs(), &err)) {
                    g.Error(this, ctx, "field '" + field + "': " + err + "; nothing was changed");
                    return 0;
                }
            }
            break;
        }
        it->second = std::move(v);
        return 0;
    }
};


} // namespace okay::vs
