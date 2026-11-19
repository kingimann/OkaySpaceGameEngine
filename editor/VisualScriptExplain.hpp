#pragma once
// Two plain-language helpers for a graph as a whole, as pure functions on the edit graph (no ImGui):
//   VsExplainGraph  - one sentence saying what the graph does, built from its blocks
//                     ("When you press space it does: Move, then Play Sound.").
//   VsWhyNotWorking - the "Why isn't it working?" checklist: the usual reasons a script does nothing, each in a
//                     plain sentence, with the block to select and, where an obvious fix exists, a Fix kind.
// Included from VisualScriptEditor.hpp after VisualScriptEasyOps.hpp (it uses VsFixNotConnected, VsIsLoose and the
// graph model). Written for this project from its own graph rules; nothing is saved in the .okayvs file.
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace okay::editor::vsed {

namespace explain {
inline bool IsEventType(const VsDef* d) { return d && !d->execIn && !d->execOut.empty(); }
/// A Quick block that starts by itself (no flow plugs), such as "Move With Keys".
inline bool IsSelfStarting(const VsEditNode& n, const VsDef* d) {
    return d && n.type.rfind("Qb", 0) == 0 && !d->execIn && d->execOut.empty();
}
inline std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
inline std::string TrimDot(std::string s) {
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    return s;
}
/// "you press space", "the game starts" ...: the moment a start block waits for.
inline std::string When(const VsEditNode& n, const VsDef& d) {
    const std::string& t = n.type;
    if (t == "OnStart") return "the game starts";
    if (t == "OnUpdate") return "every moment of the game";
    if (t == "OnKey") return "you press " + (n.arg.empty() ? std::string("a key") : n.arg);
    if (t == "OnKeyUp") return "you let go of " + (n.arg.empty() ? std::string("a key") : n.arg);
    if (t == "OnCollision" || t == "OnTrigger") return "something touches this object";
    if (t == "OnTimer") return (n.arg.empty() ? std::string("a moment") : n.arg + " seconds") + " go by";
    if (t == "OnEvent") return "the message \"" + n.arg + "\" arrives";
    std::string title = Lower(d.title);
    if (title.rfind("on ", 0) == 0) title.erase(0, 3);
    return "\"" + title + "\" happens";
}
/// What a step does, short: "Move", "Play Sound" (the block's title, in plain words).
inline std::string Step(const VsEditNode& n, const VsDef& d) { return VsPlainText(VsNodeTitle(n, d), true); }
} // namespace explain

/// One sentence (or a few, one per start block) for what the whole graph does.
inline std::string VsExplainGraph(const VsEditGraph& g, std::size_t maxSteps = 5) {
    using namespace explain;
    if (g.nodes.empty()) return "This graph is empty. Pick a goal to start with.";
    std::vector<std::string> parts;
    for (const VsEditNode& n : g.nodes) {
        const VsDef* d = FindDef(n.type);
        if (!d) continue;
        if (IsSelfStarting(n, d)) {
            const VsNodeBrief b = VsNodeBriefFor(*d);
            parts.push_back(TrimDot(b.does) + ".");
            continue;
        }
        if (!IsEventType(d)) continue;
        std::vector<std::string> steps;
        std::set<int> seen{n.id};
        std::vector<int> queue{n.id};
        bool more = false;
        for (std::size_t qi = 0; qi < queue.size(); ++qi) {
            std::vector<const VsExecLink*> out;
            for (const VsExecLink& l : g.execLinks) if (l.from == queue[qi]) out.push_back(&l);
            for (const VsExecLink* l : out) {
                if (!seen.insert(l->to).second) continue;
                const VsEditNode* t = g.find(l->to);
                const VsDef* td = t ? FindDef(t->type) : nullptr;
                if (!td) continue;
                if (steps.size() >= maxSteps) { more = true; continue; }
                steps.push_back(Step(*t, *td));
                queue.push_back(l->to);
            }
        }
        std::string s = "When " + When(n, *d) + ", ";
        if (steps.empty()) { s += "nothing happens yet (it has no step after it)."; }
        else {
            s += "it does: ";
            for (std::size_t i = 0; i < steps.size(); ++i) s += (i ? ", then " : "") + steps[i];
            s += more ? ", and more." : ".";
        }
        parts.push_back(s);
    }
    if (parts.empty()) return "Nothing starts this graph yet, so it does nothing. Add a start block, such as a key or the game starting.";
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? " " : "") + parts[i];
    return out;
}

/// What the window knows about the world around the graph.
struct VsWhyFacts {
    bool attached = true;      ///< a Visual Script component on some object runs this graph
    bool playing = false;      ///< the game is running
    bool anyEventRan = false;  ///< at least one start block has run since Play began
};

enum class VsWhyKind { EmptyGraph, NotAttached, NoStart, NotConnected, InputEmpty, NeverTriggered };
enum class VsWhyFix { None, AddStart, Connect, Attach };

struct VsWhyItem {
    VsWhyKind kind = VsWhyKind::EmptyGraph;
    std::string text;       ///< one plain sentence
    int node = -1;          ///< block to select when the row is clicked (-1: none)
    VsWhyFix fix = VsWhyFix::None;
};

/// The checklist, most important first. Empty means nothing obvious is wrong.
inline std::vector<VsWhyItem> VsWhyNotWorking(const VsEditGraph& g, const VsWhyFacts& f) {
    using namespace explain;
    std::vector<VsWhyItem> out;
    if (g.nodes.empty()) {
        out.push_back({VsWhyKind::EmptyGraph, "The graph has no blocks yet. Pick a goal on the start screen or add a block.", -1, VsWhyFix::None});
        return out;
    }
    if (!f.attached)
        out.push_back({VsWhyKind::NotAttached, "No object runs this graph. Select an object and attach the graph to it.", -1, VsWhyFix::Attach});
    bool anyStart = false;
    std::string startAdvice;
    for (const VsEditNode& n : g.nodes) {
        const VsDef* d = FindDef(n.type);
        if (IsEventType(d) || IsSelfStarting(n, d)) {
            anyStart = true;
            if (startAdvice.empty() && IsEventType(d)) startAdvice = When(n, *d);
        }
    }
    int firstLoose = -1;
    for (const VsEditNode& n : g.nodes) {
        const VsDef* d = FindDef(n.type);
        if (d && d->execIn && !VsIsPureData(d)) {
            bool fed = false;
            for (const VsExecLink& l : g.execLinks) if (l.to == n.id) fed = true;
            if (!fed) { firstLoose = n.id; break; }
        }
    }
    if (!anyStart)
        out.push_back({VsWhyKind::NoStart, "Nothing starts this graph. Add a start block, such as \"When the game starts\" or a key press.", -1,
                       VsWhyFix::AddStart});
    for (const VsEditNode& n : g.nodes) {
        const VsDef* d = FindDef(n.type);
        if (!d || !d->execIn) continue;
        bool fed = false;
        for (const VsExecLink& l : g.execLinks) if (l.to == n.id) fed = true;
        if (!fed)
            out.push_back({VsWhyKind::NotConnected, "\"" + Step(n, *d) + "\" is not connected after anything, so it never runs.", n.id, VsWhyFix::Connect});
    }
    for (const VsEditNode& n : g.nodes) {
        const VsDef* d = FindDef(n.type);
        if (!d) continue;
        if (const char* w = VsRequiredWhat(n.type, -1))
            if (!VsArgFilled(n.arg))
                out.push_back({VsWhyKind::InputEmpty, "\"" + Step(n, *d) + "\" needs a " + w + " typed on it.", n.id, VsWhyFix::None});
        for (int p = 0; p < (int)d->dataIn.size(); ++p)
            if (const char* w = VsRequiredWhat(n.type, p))
                if (!VsInputWired(g, n.id, p) && !VsInputHasValue(n, p))
                    out.push_back({VsWhyKind::InputEmpty, "\"" + Step(n, *d) + "\" needs a " + w + " plugged into \"" + d->dataIn[(std::size_t)p] + "\".", n.id, VsWhyFix::None});
    }
    if (f.playing && !f.anyEventRan && anyStart)
        out.push_back({VsWhyKind::NeverTriggered,
                       startAdvice.empty() ? std::string("The game is playing, but no start block has run yet. Do the thing it waits for.")
                                           : "The game is playing, but no start block has run yet. Make this happen: " + startAdvice + ".",
                       -1, VsWhyFix::None});
    (void)firstLoose;
    return out;
}

/// Apply a Fix kind to the graph (Attach is done by the window, it needs the scene). True when the graph changed.
inline bool VsApplyWhyFix(VsEditGraph& g, const VsWhyItem& it) {
    if (it.fix == VsWhyFix::Connect && it.node >= 0) return VsFixNotConnected(g, it.node);
    if (it.fix == VsWhyFix::AddStart) {
        const auto pos = VsFreeCorner(g);
        const int id = g.add("OnStart", pos.first, pos.second);
        for (const VsEditNode& n : g.nodes) {
            const VsDef* d = FindDef(n.type);
            if (n.id == id || !d || !d->execIn) continue;
            bool fed = false;
            for (const VsExecLink& l : g.execLinks) if (l.to == n.id) fed = true;
            if (!fed) { g.linkExec(id, 0, n.id); break; }
        }
        return true;
    }
    return false;
}

} // namespace okay::editor::vsed
