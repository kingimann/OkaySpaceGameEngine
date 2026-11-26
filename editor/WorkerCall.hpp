#pragma once
// One call run on a thread of its own, its result collected on the editor's thread in a later frame: for work that waits on the
// network or reads a whole project and must never hold a frame. Used by the update check and the update install
// (editor/main.cpp: CheckLatest waits up to 20 s to connect and 60 s per read, three tries; the owner's okay_editor_perf.log,
// 2026-11-25) and by the Problems list's project-wide check (editor/ProjectDiagRunner.cpp).
//
//   WorkerCall call;
//   auto out = std::make_shared<Result>();
//   call.Start([out] { *out = SlowThing(); }, "okay-slow-thing");   // returns at once
//   ... every frame: if (call.Collect()) Use(*out);                  // true once, after SlowThing returned
//
// The call must only touch what it owns (capture shared_ptr or values, never the editor's globals). Dropping the WorkerCall while
// its call runs lets the call finish on its own; nothing is collected then. Threads through okay::thr (CLAUDE.md rule 21a: no
// <thread> or <mutex> here). Written from the plain idea of a one-shot background job; nothing copied.
#include <functional>

namespace okay::thr { struct Thread; }

namespace oked {

class WorkerCall {
public:
    WorkerCall() = default;
    ~WorkerCall() { Drop(); }
    WorkerCall(const WorkerCall&) = delete;
    WorkerCall& operator=(const WorkerCall&) = delete;

    /// Start `fn` on its own thread and return at once. False (nothing started) while an earlier call is not collected yet.
    /// When the system gives no thread, `fn` runs here before Start returns (as the code did before), and Collect is true next.
    bool Start(std::function<void()> fn, const char* name);
    /// Started and not collected yet (it may have finished: Collect says).
    bool Busy() const { return m_state != nullptr; }
    /// True once, on the first call after `fn` returned: what `fn` wrote may be read from then on.
    bool Collect();
    /// Let a call that is still running go: it finishes on its own and nothing is collected.
    void Drop();

    struct State;   // the call and its flag, shared with the thread (WorkerCall.cpp)

private:
    State* m_state = nullptr;
    okay::thr::Thread* m_thread = nullptr;
};

}  // namespace oked
