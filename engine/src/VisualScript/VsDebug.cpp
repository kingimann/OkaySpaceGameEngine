#include "okay/VisualScript/VsDebug.hpp"
#include "okay/VisualScript/NodeGraph.hpp"

namespace okay::vs {

VsDebugger& VsDebugger::Get() {
    static VsDebugger d;
    return d;
}

void VsDebugger::SetBreakpoint(const std::string& graphKey, int fileId, bool on) {
    if (on) m_bps.insert({graphKey, fileId});
    else m_bps.erase({graphKey, fileId});
}

bool VsDebugger::HasBreakpoint(const std::string& graphKey, int fileId) const {
    return !m_bps.empty() && m_bps.count({graphKey, fileId}) > 0;
}

void VsDebugger::RequestStep(Step s) {
    m_step = s;
    m_stepGraph = m_lastGraph;
    m_stepDepth = m_lastDepth;
}

void VsDebugger::Forget(const NodeGraph* g) {
    if (m_stepGraph == g) m_stepGraph = nullptr;
    if (m_lastGraph == g) m_lastGraph = nullptr;
}

void VsDebugger::Check(NodeGraph& g, VsNode& n, VsContext& ctx) {
    if (!Active()) return;
    const int depth = g.CallDepth();
    bool stop = !g.debugKey.empty() && n.fileId >= 0 && HasBreakpoint(g.debugKey, n.fileId);
    // Over / Out: once the stepped event has finished (or its graph is gone), the
    // next step node anywhere is "the next step".
    const bool sameRun = m_stepGraph == &g;
    const bool stepDone = !m_stepGraph || (!sameRun && !m_stepGraph->Running());
    switch (m_step) {
        case Step::None: break;
        case Step::Into: stop = true; break;
        case Step::Over: if (stepDone || (sameRun && depth <= m_stepDepth)) stop = true; break;
        case Step::Out:  if (stepDone || (sameRun && depth < m_stepDepth)) stop = true; break;
    }
    if (!stop) return;
    m_step = Step::None;
    m_lastGraph = &g;
    m_lastDepth = depth;
    onStop(g, n, ctx);
}

} // namespace okay::vs
