#pragma once
// The text-script (OkayScript) debugger's engine half: breakpoints by line,
// stepping, a call stack, variable inspection and read-only watch expressions.
//
// It is the text-language twin of the visual-script debugger (VsDebug.hpp) and
// reuses its pieces: the same `Step` enum (None = continue, Into, Over, Out),
// the same key + number breakpoint set (a script path + a LINE instead of a
// graph path + a node id), the same "onStop handler freezes the game, then
// returns to carry on" design, and the same variable shape (sorted name/value
// pairs) so the editor can show one panel for both.
//
// Cost while nothing is attached: one test of a plain bool per statement
// (`ScriptDebugger::s_attached`). It is only true when an onStop handler is set
// AND a breakpoint exists, a step is pending, or pause-on-error is on. Scripts
// only run in Play, and the editor sets the handler only while Playing.
//
// Not covered yet: statements inside coroutines (StartCoroutine) are not
// stopped at, and a watch expression sees the innermost frame's variables.
#include "okay/VisualScript/VsDebug.hpp"
#include "okay/VisualScript/VsValue.hpp"
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// One entry of the call stack (innermost first in ScriptDebugContext::Frames()).
struct ScriptDebugFrame {
    std::string name;   ///< "Update()", "Heal()", "a lambda", or "(script)" for the entry point
    int line = 0;       ///< the line this frame is on (the stopped line for the innermost)
};

using ScriptDebugVars = std::vector<std::pair<std::string, vs::VsValue>>;   // same shape as the visual debugger's

/// A live view of the paused script. Only valid inside the onStop handler.
class ScriptDebugContext {
public:
    virtual ~ScriptDebugContext() = default;
    /// Call stack, innermost first.
    virtual std::vector<ScriptDebugFrame> Frames() const = 0;
    /// Variables of a frame (0 = innermost), sorted by name. The entry frame's
    /// "locals" are the script's globals.
    virtual ScriptDebugVars Locals(int frame = 0) const = 0;
    virtual ScriptDebugVars Globals() const = 0;
    /// Evaluate an expression read-only in the innermost frame. Assignments,
    /// ++/--, `new`, lambdas and calls to anything but a few pure helpers
    /// (len, abs, min, max, floor, ceil, round, sqrt, clamp, str) are refused:
    /// returns false and fills `error`.
    virtual bool Evaluate(const std::string& expression, vs::VsValue& out, std::string* error = nullptr) const = 0;
};

enum class ScriptStopReason { Breakpoint, Step, Error };

struct ScriptStop {
    std::string key;            ///< the script's path (as the Script component holds it)
    int line = 0;
    ScriptStopReason reason = ScriptStopReason::Breakpoint;
    std::string message;        ///< the error text when reason == Error
    ScriptDebugContext* ctx = nullptr;
};

class ScriptDebugger {
public:
    static ScriptDebugger& Get();
    using Step = vs::VsDebugger::Step;

    /// The breakpoint key for a script file: its absolute path, normalised, with '/'
    /// separators. The Script component and the editor both use this, so a breakpoint
    /// set in a script tab matches the script a scene object runs.
    static std::string KeyForPath(const std::string& path);

    // ---- Breakpoints (script path + 1-based line) ----
    void SetBreakpoint(const std::string& key, int line, bool on);
    bool HasBreakpoint(const std::string& key, int line) const;
    void ToggleBreakpoint(const std::string& key, int line) { SetBreakpoint(key, line, !HasBreakpoint(key, line)); }
    void ClearBreakpoints();
    const std::set<std::pair<std::string, int>>& Breakpoints() const { return m_bps; }

    /// Where to stop next, counted from the stop you are at. Call it while
    /// stopped (inside or after onStop), before the game carries on.
    void RequestStep(Step s);
    Step PendingStep() const { return m_step; }

    /// Also stop when a script error is raised (once per error), before it unwinds.
    /// Note it fires even for an error a try/catch would handle.
    void SetPauseOnError(bool on);
    bool PauseOnError() const { return m_pauseOnError; }

    /// Off: nothing stops (used to finish a frozen frame when Play stops).
    void SetEnabled(bool on);
    bool Enabled() const { return m_enabled; }

    /// The editor's "stopped here" handler. Nothing stops while it is not set.
    /// An exception thrown out of it is caught and logged; the script carries on.
    void SetOnStop(std::function<void(const ScriptStop&)> fn);

    /// The cheap per-statement test (also what the VM reads).
    static inline bool s_attached = false;

    // ---- VM side ----
    /// A statement at `line` of script `key` is about to run at call depth `depth`.
    /// Returns true when it should stop (breakpoint / step); the VM then calls Stopped().
    bool ShouldStop(const std::string& key, int line, int depth);
    /// The VM stopped: record the depth for the next step and run the handler.
    void Stopped(const ScriptStop& stop, int depth);
    bool InStop() const { return m_inStop; }
    bool WantsErrorStops() const { return m_pauseOnError && m_enabled && m_onStop; }

private:
    void Refresh() { s_attached = m_enabled && m_onStop && (!m_bps.empty() || m_step != Step::None || m_pauseOnError); }
    std::set<std::pair<std::string, int>> m_bps;
    Step m_step = Step::None;
    int m_stepDepth = 0;
    bool m_enabled = true, m_pauseOnError = false, m_inStop = false;
    std::function<void(const ScriptStop&)> m_onStop;
    // The statement that ran just before, so one line with several statements stops once.
    std::string m_prevKey; int m_prevLine = -1; int m_prevDepth = -1;
};

} // namespace okay
