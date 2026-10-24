#pragma once
// The visual-script debugger's engine half: breakpoints and stepping.
//
// Breakpoints are kept by graph file (the .okayvs path a Visual Script loads)
// and node id, so they survive the graph being reloaded. Before each step node
// runs, the graph asks the debugger whether to stop there; when it should, the
// debugger calls `onStop` - the editor's handler, which freezes the game (the
// tick runs in a fiber it switches out of) and shows where it stopped. When the
// handler returns, the node runs as usual.
//
// Nothing here costs anything while no breakpoint is set and nobody is
// stepping; an exported game never sets either.
#include <functional>
#include <set>
#include <string>
#include <utility>

namespace okay::vs {

class NodeGraph;
class VsNode;
struct VsContext;

class VsDebugger {
public:
    static VsDebugger& Get();

    /// Where to stop next, counted from the node execution last stopped at.
    enum class Step { None, Into, Over, Out };

    // ---- Breakpoints (graph file path + node id) ----
    void SetBreakpoint(const std::string& graphKey, int fileId, bool on);
    bool HasBreakpoint(const std::string& graphKey, int fileId) const;
    void ToggleBreakpoint(const std::string& graphKey, int fileId) { SetBreakpoint(graphKey, fileId, !HasBreakpoint(graphKey, fileId)); }
    void ClearBreakpoints() { m_bps.clear(); }
    const std::set<std::pair<std::string, int>>& Breakpoints() const { return m_bps; }

    /// Ask to stop again at the next step node (Into), at the next one not inside
    /// a function called from here (Over), or after this function returns (Out).
    /// Call it while stopped, before letting the game carry on.
    void RequestStep(Step s);
    Step PendingStep() const { return m_step; }

    /// Off: nothing stops (used to finish a frozen frame when Play is stopped).
    bool enabled = true;
    /// The editor's "stop here" handler; nothing stops while it isn't set.
    std::function<void(NodeGraph&, VsNode&, VsContext&)> onStop;

    /// Cheap test the graph makes before every step node.
    bool Active() const { return enabled && onStop && (!m_bps.empty() || m_step != Step::None); }
    /// Called by NodeGraph::Run before `node` runs: stops there when a breakpoint
    /// or a pending step says so.
    void Check(NodeGraph& graph, VsNode& node, VsContext& ctx);
    /// A graph is going away: drop any reference to it.
    void Forget(const NodeGraph* graph);

private:
    std::set<std::pair<std::string, int>> m_bps;
    Step m_step = Step::None;
    const NodeGraph* m_stepGraph = nullptr;   // where the step was asked from
    int m_stepDepth = 0;                      // its function-call depth
    const NodeGraph* m_lastGraph = nullptr;   // the last stop
    int m_lastDepth = 0;
};

} // namespace okay::vs
