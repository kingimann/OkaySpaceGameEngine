#include "okay/Scripting/ScriptDebug.hpp"
#include "okay/Core/Log.hpp"
#include <filesystem>

namespace okay {

ScriptDebugger& ScriptDebugger::Get() {
    static ScriptDebugger d;
    return d;
}

std::string ScriptDebugger::KeyForPath(const std::string& path) {
    if (path.empty()) return {};
    std::error_code ec;
    std::filesystem::path p = std::filesystem::absolute(std::filesystem::path(path), ec);
    if (ec) p = path;
    return p.lexically_normal().generic_string();
}

void ScriptDebugger::SetBreakpoint(const std::string& key, int line, bool on) {
    if (on) m_bps.insert({key, line}); else m_bps.erase({key, line});
    Refresh();
}
bool ScriptDebugger::HasBreakpoint(const std::string& key, int line) const {
    return !m_bps.empty() && m_bps.count({key, line}) > 0;
}
void ScriptDebugger::ClearBreakpoints() { m_bps.clear(); Refresh(); }
void ScriptDebugger::RequestStep(Step s) {
    m_step = s;
    m_stepDepth = m_prevDepth < 0 ? 0 : m_prevDepth;
    m_prevLine = -1;   // the next statement counts as new even on the same line
    Refresh();
}
void ScriptDebugger::SetPauseOnError(bool on) { m_pauseOnError = on; Refresh(); }
void ScriptDebugger::SetEnabled(bool on) { m_enabled = on; Refresh(); }
void ScriptDebugger::SetOnStop(std::function<void(const ScriptStop&)> fn) { m_onStop = std::move(fn); Refresh(); }

bool ScriptDebugger::ShouldStop(const std::string& key, int line, int depth) {
    if (m_inStop || !m_enabled || !m_onStop) return false;
    const bool sameAsPrev = line == m_prevLine && depth == m_prevDepth && key == m_prevKey;
    m_prevKey = key; m_prevLine = line; m_prevDepth = depth;
    if (sameAsPrev) return false;
    bool stop = !key.empty() && HasBreakpoint(key, line);
    switch (m_step) {
        case Step::None: break;
        case Step::Into: stop = true; break;
        case Step::Over: if (depth <= m_stepDepth) stop = true; break;
        case Step::Out:  if (depth < m_stepDepth) stop = true; break;
    }
    return stop;
}

void ScriptDebugger::Stopped(const ScriptStop& stop, int depth) {
    m_step = Step::None;
    m_stepDepth = depth;
    m_prevDepth = depth;
    Refresh();
    if (!m_onStop) return;
    m_inStop = true;
    try { m_onStop(stop); }
    catch (const std::exception& e) { Log::Error("OkayScript debugger: the stop handler failed: ", e.what()); }
    catch (...) { Log::Error("OkayScript debugger: the stop handler failed"); }
    m_inStop = false;
    // The handler may have asked for a step (RequestStep recorded m_prevDepth).
    Refresh();
}

} // namespace okay
