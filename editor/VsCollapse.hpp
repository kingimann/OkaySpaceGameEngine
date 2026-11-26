#pragma once
// Collapse to Function: the selected blocks become the body of a new function in the same graph, and one Call Function
// block takes their place. Values wired in from outside the selection become the function's parameters (one per
// output that fed it); values the selection gave to blocks outside become its return values. The flow that came in
// now enters the Call, and the flow that left now leaves the Call. Locals (Set / Get Local) and Return already exist
// for functions (engine/include/okay/VisualScript/NodesFunctions.hpp); this only rewires the graph.
//
// The graph must do the same thing before and after, so a selection is refused (graph unchanged, `why` says what to
// change) when that can't be promised: no step in it, an event or a function edge in it, flow entering at more than
// one block, flow leaving to more than one place, or leaving from a block that also continues inside (a Sequence's
// first output, a loop's Body). Arguments are read when the function is called, the way a call always reads them.
//
// No ImGui: tests run it. Included from inside VisualScriptEditor.hpp after the graph model. Written for this project
// from its own function rules (Function / Call Function / Return, VsTypes.hpp FunctionSig).
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace okay::editor::vsed {

struct VsCollapseResult {
    bool ok = false;
    std::string why;                 // when refused: what to change, in plain words
    std::string name;                // the new function's name
    int functionNode = -1, callNode = -1, returnNode = -1;
    int params = 0, returns = 0;
};

/// A function name not used by any Function block in `g`: `base`, then base2, base3...
inline std::string VsUniqueFunctionName(const VsEditGraph& g, const std::string& base = "NewFunction") {
    auto used = [&](const std::string& n) {
        for (const auto& x : g.nodes) if (x.type == "FunctionStart" && okay::vs::FunctionNameOf(x.arg) == n) return true;
        return false;
    };
    if (!used(base)) return base;
    for (int i = 2; i < 10000; ++i) if (!used(base + std::to_string(i))) return base + std::to_string(i);
    return base + "_";
}

/// A pin label as a parameter / return name: letters, digits and _, starting with a letter, lower-case first letter.
inline std::string VsSigIdent(const std::string& label, const char* fallback) {
    std::string s;
    bool up = false;
    for (char c : label) {
        if (std::isalnum((unsigned char)c)) { s += up && !s.empty() ? (char)std::toupper((unsigned char)c) : c; up = false; }
        else up = true;
    }
    if (s.empty() || std::isdigit((unsigned char)s[0])) s = fallback + s;
    s[0] = (char)std::tolower((unsigned char)s[0]);
    return s;
}

/// True for a block that starts a chain by itself (an event, a quick block with no flow in).
inline bool VsIsStartBlock(const VsEditNode& n) {
    if (!VsEntryName(n.type, n.arg).empty() || VsStartsOnItsOwn(n.type)) return true;
    const VsDef* d = FindDef(n.type);
    return d && !d->execIn && !d->execOut.empty();
}

/// Blocks whose outputs are alternatives (only one of them runs), so leaving from two of them is still one way out.
inline bool VsOutputsAreAlternatives(const std::string& type) {
    return type == "Branch" || type == "SwitchString" || type == "SwitchCases";
}

inline VsCollapseResult VsCollapseToFunction(VsEditGraph& g, const std::set<int>& selIn, const std::string& wantName = "") {
    VsCollapseResult r;
    auto refuse = [&](const std::string& why) { r.ok = false; r.why = why; return r; };
    std::set<int> sel;
    for (int id : selIn) if (g.find(id)) sel.insert(id);
    if (sel.empty()) return refuse("Select the blocks to put in the function first.");
    bool anyStep = false;
    for (int id : sel) {
        const VsEditNode* n = g.find(id);
        const VsDef* d = VsDefFor(g, *n);
        if (!d) return refuse("Block #" + std::to_string(id) + " (" + n->type + ") is not known here, so it can't be moved.");
        if (n->type == "FunctionStart" || n->type == "Return")
            return refuse("The selection has a Function or Return block. Select the steps inside a function, not its edges.");
        if (VsIsStartBlock(*n)) return refuse("The selection has an event (" + std::string(d->title ? d->title : n->type.c_str()) +
                                              "). Select the steps after it; the event stays and calls the function.");
        if (d->execIn) anyStep = true;
    }
    if (!anyStep) return refuse("Select at least one step (a block the flow runs through). A function is called by the flow.");

    // ---- Flow: one way in, one way out ----
    int entry = -1;
    std::vector<VsExecLink> entries, exits;
    for (const VsExecLink& l : g.execLinks) {
        const bool fromIn = sel.count(l.from) > 0, toIn = sel.count(l.to) > 0;
        if (!fromIn && toIn) {
            if (entry >= 0 && entry != l.to) return refuse("The flow enters the selection at more than one block. A function has one way in.");
            entry = l.to; entries.push_back(l);
        } else if (fromIn && !toIn) exits.push_back(l);
    }
    if (entry < 0) return refuse("Nothing runs the selected steps (no flow wire comes into them from outside).");
    int after = -1;
    for (const VsExecLink& l : exits) {
        if (after >= 0 && after != l.to) return refuse("The flow leaves the selection to more than one place. A function has one way out.");
        after = l.to;
    }
    for (const VsExecLink& l : exits) {
        const VsEditNode* s = g.find(l.from);
        if (VsOutputsAreAlternatives(s->type)) continue;
        for (const VsExecLink& m : g.execLinks)
            if (m.from == l.from && m.pin != l.pin)
                return refuse("Block #" + std::to_string(l.from) + " leaves the selection from one output and goes on from another "
                              "(a Sequence, or a loop's Body). Select the whole loop or sequence, or less.");
    }
    // A Set / Get Local in the selection would read the new function's locals instead of the ones it reads now.
    for (int id : sel) {
        const std::string& t = g.find(id)->type;
        if (t == "SetLocal" || t == "GetLocal") {
            bool inFunction = false;
            for (const auto& n : g.nodes) if (n.type == "FunctionStart" && VsDownstream(g, n.id).count(entry)) inFunction = true;
            if (inFunction) return refuse("The selection uses local variables of the function it is in. Move them by hand.");
        }
    }

    // ---- Values in (parameters) and out (returns) ----
    struct Port { int node, pin; okay::vs::PinType type; std::string name; };
    std::vector<Port> params, rets;
    std::vector<VsDataLink> inLinks, outLinks;
    auto portIndex = [](std::vector<Port>& v, int node, int pin) {
        for (std::size_t i = 0; i < v.size(); ++i) if (v[i].node == node && v[i].pin == pin) return (int)i;
        return -1;
    };
    auto uniqueName = [](std::vector<Port>& v, std::string n) {
        std::string base = n; int k = 2;
        for (;;) {
            bool clash = false;
            for (const Port& p : v) if (p.name == n) clash = true;
            if (!clash) return n;
            n = base + std::to_string(k++);
        }
    };
    for (const VsDataLink& l : g.dataLinks) {
        const bool consIn = sel.count(l.consumer) > 0, prodIn = sel.count(l.producer) > 0;
        if (consIn && !prodIn) {
            inLinks.push_back(l);
            if (portIndex(params, l.producer, l.outPin) < 0) {
                const VsEditNode* c = g.find(l.consumer);
                const VsDef* cd = VsDefFor(g, *c);
                const std::string label = cd && l.inPin < (int)cd->dataIn.size() ? cd->dataIn[(std::size_t)l.inPin] : "";
                params.push_back({l.producer, l.outPin, VsOutPinType(g, l.producer, l.outPin), uniqueName(params, VsSigIdent(label, "in"))});
            }
        } else if (prodIn && !consIn) {
            outLinks.push_back(l);
            if (portIndex(rets, l.producer, l.outPin) < 0) {
                const VsEditNode* p = g.find(l.producer);
                const VsDef* pd = VsDefFor(g, *p);
                const std::string label = pd && l.outPin < (int)pd->dataOut.size() ? pd->dataOut[(std::size_t)l.outPin] : "";
                rets.push_back({l.producer, l.outPin, VsOutPinType(g, l.producer, l.outPin), uniqueName(rets, VsSigIdent(label, "out"))});
            }
        }
    }
    // Return values need a Return at the end of the flow. With no flow leaving, the end is the one step that goes nowhere.
    std::vector<std::pair<int, int>> tails;   // (node, pin) that lead to the Return
    for (const VsExecLink& l : exits) tails.push_back({l.from, l.pin});
    if (exits.empty() && !rets.empty()) {
        std::vector<int> ends;
        for (int id : sel) {
            const VsDef* d = VsDefFor(g, *g.find(id));
            if (!d || !d->execIn || d->execOut.empty()) continue;
            bool goes = false;
            for (const VsExecLink& l : g.execLinks) if (l.from == id) goes = true;
            if (!goes) ends.push_back(id);
        }
        if (ends.size() != 1) return refuse("The selection gives values to blocks outside it, but its flow has no single end to return them from.");
        tails.push_back({ends[0], 0});
    }

    // ---- Build ----
    r.name = wantName.empty() ? VsUniqueFunctionName(g) : wantName;
    if (!okay::vs::VsValidIdent(r.name)) return refuse("'" + r.name + "' isn't a valid function name.");
    for (const auto& n : g.nodes) if (n.type == "FunctionStart" && okay::vs::FunctionNameOf(n.arg) == r.name)
        return refuse("There is already a function named '" + r.name + "'.");
    std::string sig = r.name + "(";
    for (std::size_t i = 0; i < params.size(); ++i) sig += (i ? ", " : "") + params[i].name + ": " + okay::vs::PinTypeName(params[i].type);
    sig += ")";
    if (!rets.empty()) {
        sig += " -> ";
        for (std::size_t i = 0; i < rets.size(); ++i) sig += (i ? ", " : "") + rets[i].name + ": " + okay::vs::PinTypeName(rets[i].type);
    }
    // Where things go: the call where the first step was (so it lands in the flow, not on a block left outside); the body moved below everything, the Function block on its left.
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, graphMaxY = -1e9f;
    for (int id : sel) { const VsEditNode* n = g.find(id); minX = std::min(minX, n->x); minY = std::min(minY, n->y); maxX = std::max(maxX, n->x); }
    for (const auto& n : g.nodes) graphMaxY = std::max(graphMaxY, n.y);
    const float callX = g.find(entry)->x, callY = g.find(entry)->y;
    const float dy = graphMaxY + 260.0f - minY;
    for (int id : sel) g.find(id)->y += dy;

    r.functionNode = g.add("FunctionStart", minX - 280.0f, minY + dy);
    g.find(r.functionNode)->arg = sig;
    r.callNode = g.add("CallFunction", callX, callY);
    g.find(r.callNode)->arg = r.name;
    r.params = (int)params.size(); r.returns = (int)rets.size();

    // Flow in: outside -> Call; Function -> the first step.
    for (VsExecLink& l : g.execLinks) if (!sel.count(l.from) && l.to == entry) l.to = r.callNode;
    g.linkExec(r.functionNode, 0, entry);
    // Flow out: each exit goes to the Return; the Call goes on to where the flow went.
    if (!tails.empty() && (!rets.empty() || !exits.empty())) {
        r.returnNode = g.add("Return", maxX + 300.0f, minY + dy);
        g.find(r.returnNode)->arg = r.name;
        for (VsExecLink& l : g.execLinks)
            if (sel.count(l.from) && !sel.count(l.to) && l.to != r.returnNode) l.to = r.returnNode;
        if (exits.empty()) for (const auto& t : tails) g.linkExec(t.first, t.second, r.returnNode);
    }
    if (after >= 0) g.linkExec(r.callNode, 0, after);
    // Values in: the Function's parameter outputs feed what the outside fed; the outside feeds the Call.
    for (const VsDataLink& l : inLinks) {
        const int i = portIndex(params, l.producer, l.outPin);
        g.linkData(l.consumer, l.inPin, r.functionNode, i);
    }
    for (std::size_t i = 0; i < params.size(); ++i) g.linkData(r.callNode, (int)i, params[i].node, params[i].pin);
    // Values out: the Return takes them; the outside reads them from the Call.
    for (std::size_t i = 0; i < rets.size(); ++i) g.linkData(r.returnNode, (int)i, rets[i].node, rets[i].pin);
    for (const VsDataLink& l : outLinks) {
        const int i = portIndex(rets, l.producer, l.outPin);
        g.linkData(l.consumer, l.inPin, r.callNode, i);
    }
    g.dirty = true;
    r.ok = true;
    return r;
}

/// Would the selection collapse? (Tried on a copy.) `why` gets the reason when it would not.
inline bool VsCanCollapse(const VsEditGraph& g, const std::set<int>& sel, std::string* why = nullptr) {
    VsEditGraph copy = g;
    const VsCollapseResult r = VsCollapseToFunction(copy, sel);
    if (why) *why = r.why;
    return r.ok;
}

} // namespace okay::editor::vsed
