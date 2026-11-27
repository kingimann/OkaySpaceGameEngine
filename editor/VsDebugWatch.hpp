#pragma once
// The Visual Script debugger's watch side (no ImGui): what a value reads like on screen, the watch list, the value a
// wire carries, and which wires the run went through this frame (lit while playing).
// It reads the trace the running graph keeps while NodeGraph::Tracing() is on (VsNode::execCount and lastOut); it
// never runs a node, so looking at a value can't change what the game does.
// Included from inside VisualScriptModel.hpp after the graph model (VsEditGraph). Written for this project from its
// own debugger rules (engine/include/okay/VisualScript/VsDebug.hpp).
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/Scene/GameObject.hpp"
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace okay::editor::vsed {

/// A value as the canvas shows it beside a pin or in the watch list: numbers to 3 digits, vectors as (x, y, z), an
/// object by name ("(no object)" / "(destroyed)"), text as-is. Long text is cut at `maxLen` with an ellipsis (0 = keep).
inline std::string VsLiveValueText(const okay::vs::VsValue& val, std::size_t maxLen = 22) {
    std::string v = val.AsString();
    if (val.IsVec3()) {
        const okay::Vec3 q = val.AsVec3();
        char b[64]; std::snprintf(b, sizeof b, "(%.2f, %.2f, %.2f)", q.x, q.y, q.z); v = b;
    } else if (val.IsObject()) {
        if (!val.AsObjectId()) v = "(no object)";
        else if (v.empty()) v = "(destroyed)";
    } else if (!val.IsString() && v != "true" && v != "false" && !val.IsArray() && !val.IsMap()) {
        char b[32]; std::snprintf(b, sizeof b, "%.3g", val.AsFloat()); v = b;
    }
    if (maxLen > 0 && v.size() > maxLen) v = v.substr(0, maxLen - 1) + "\xE2\x80\xA6";
    return v;
}

/// The running node for editor id `fileId` (nullptr when there is no live graph or the node is not in it).
inline const okay::vs::VsNode* VsLiveNode(const okay::vs::NodeGraph* live, int fileId) {
    if (!live) return nullptr;
    const int rid = live->NodeByFileId(fileId);
    return rid >= 0 ? live->Node(rid) : nullptr;
}

/// The last value output `pin` of node `fileId` gave in the live graph. False when it has not given one yet.
inline bool VsLiveValue(const okay::vs::NodeGraph* live, int fileId, int pin, okay::vs::VsValue& out) {
    const okay::vs::VsNode* n = VsLiveNode(live, fileId);
    if (!n || pin < 0 || pin >= (int)n->lastOut.size()) return false;
    out = n->lastOut[(std::size_t)pin];
    return true;
}

/// One watched value: a data output of a node (editor id + pin).
struct VsWatch { int node = -1; int pin = 0; };

/// The watch list of one graph. Add from a node's menu ("Watch values"), remove from the list.
struct VsWatchList {
    std::vector<VsWatch> items;
    bool has(int node, int pin) const {
        for (const VsWatch& w : items) if (w.node == node && w.pin == pin) return true;
        return false;
    }
    /// Add it, or take it away when it is already watched. True when it is watched afterwards.
    bool toggle(int node, int pin) {
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].node == node && items[i].pin == pin) { items.erase(items.begin() + (long)i); return false; }
        items.push_back({node, pin});
        return true;
    }
    void remove(std::size_t i) { if (i < items.size()) items.erase(items.begin() + (long)i); }
    /// Drop watches on nodes the graph no longer has.
    template <class Graph> void prune(const Graph& g) {
        std::vector<VsWatch> keep;
        for (const VsWatch& w : items) if (VsFind(g, w.node)) keep.push_back(w);
        items.swap(keep);
    }
};

/// One row of the watch list as the panel shows it.
struct VsWatchRow { std::string label, value; bool known = false; };

/// The watch list read against the live graph: "Add #3 . Out = 5". A watch whose node has not run says so.
template <class Graph>
inline std::vector<VsWatchRow> VsWatchRows(const Graph& g, const okay::vs::NodeGraph* live, const VsWatchList& list) {
    std::vector<VsWatchRow> out;
    for (const VsWatch& w : list.items) {
        VsWatchRow r;
        const VsEditNode* n = VsFind(g, w.node);
        const VsDef* d = n ? VsDefFor(g, *n) : nullptr;
        const std::string title = d && d->title ? std::string(d->title) : n ? n->type : std::string("(deleted)");
        const std::string pinName = d && w.pin >= 0 && w.pin < (int)d->dataOut.size() && d->dataOut[(std::size_t)w.pin][0]
                                  ? std::string(d->dataOut[(std::size_t)w.pin]) : "out " + std::to_string(w.pin);
        r.label = title + " #" + std::to_string(w.node) + " . " + pinName;
        okay::vs::VsValue v;
        r.known = n && VsLiveValue(live, w.node, w.pin, v);
        r.value = !live ? std::string("(not playing)") : !n ? std::string("(node deleted)")
                : r.known ? VsLiveValueText(v, 60) : std::string("(not run yet)");
        out.push_back(std::move(r));
    }
    return out;
}

/// What the value wire `dataLinks[index]` carried last ("" when it has not carried one yet). Shown when the mouse is
/// over the wire while the game is paused or playing.
template <class Graph>
inline std::string VsWireValueText(const Graph& g, const okay::vs::NodeGraph* live, int index) {
    if (index < 0 || index >= (int)g.dataLinks.size()) return {};
    const auto& l = g.dataLinks[(std::size_t)index];
    okay::vs::VsValue v;
    if (!VsLiveValue(live, l.producer, l.outPin, v)) return {};
    return VsLiveValueText(v, 80);
}

/// The editor ids of the nodes that ran since the last call (their run count moved), updating `seen`.
template <class Graph>
inline std::set<int> VsRanSince(const Graph& g, const okay::vs::NodeGraph* live, std::map<int, unsigned>& seen) {
    std::set<int> ran;
    if (!live) return ran;
    for (const auto& n : g.nodes) {
        const okay::vs::VsNode* ln = VsLiveNode(live, n.id);
        if (!ln) continue;
        unsigned& s = seen[n.id];
        if (ln->execCount != s) { ran.insert(n.id); s = ln->execCount; }
    }
    return ran;
}

/// Which wires the run went through, from the nodes that ran (`ran`): a flow wire when both its ends ran, a value
/// wire when the step reading it ran and its producer has given a value. Indexes into execLinks / dataLinks.
struct VsLitWires { std::vector<char> exec, data; };
template <class Graph>
inline VsLitWires VsLitPath(const Graph& g, const okay::vs::NodeGraph* live, const std::set<int>& ran) {
    VsLitWires w;
    w.exec.assign(g.execLinks.size(), 0);
    w.data.assign(g.dataLinks.size(), 0);
    for (std::size_t i = 0; i < g.execLinks.size(); ++i)
        w.exec[i] = ran.count(g.execLinks[i].from) && ran.count(g.execLinks[i].to) ? 1 : 0;
    // A value block runs when a step reads it, so "lit" follows the reader back through value wires.
    std::set<int> readers = ran;
    for (int pass = 0; pass < 8; ++pass) {
        bool grew = false;
        for (std::size_t i = 0; i < g.dataLinks.size(); ++i) {
            const auto& l = g.dataLinks[i];
            if (w.data[i] || !readers.count(l.consumer)) continue;
            okay::vs::VsValue v;
            if (!VsLiveValue(live, l.producer, l.outPin, v)) continue;
            w.data[i] = 1;
            if (readers.insert(l.producer).second) grew = true;
        }
        if (!grew) break;
    }
    return w;
}

/// The call stack at a stop, innermost last, as one line per frame: the event, then each function with its arguments.
inline std::vector<std::string> VsCallStackLines(const std::string& event, const std::vector<okay::vs::NodeGraph::DebugFrame>& frames) {
    std::vector<std::string> out;
    out.push_back("event " + (event.empty() ? std::string("?") : event));
    for (const auto& f : frames) {
        std::string s = f.name + "(";
        for (std::size_t i = 0; i < f.args.size(); ++i) s += (i ? ", " : "") + VsLiveValueText(f.args[i], 16);
        out.push_back(s + ")");
    }
    return out;
}

} // namespace okay::editor::vsed
