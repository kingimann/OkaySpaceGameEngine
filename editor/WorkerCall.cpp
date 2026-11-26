// editor/WorkerCall.hpp. Written from the plain idea of a one-shot background job whose flag says when it is done; nothing copied.
#include "WorkerCall.hpp"

#include "../shared/OkayThreads.hpp"

#include <atomic>
#include <utility>

namespace oked {

struct WorkerCall::State {
    std::function<void()> fn;
    std::atomic<int> state{0};   // 0 running, 1 finished, 2 the WorkerCall let it go (whoever comes second frees the State)
};

namespace {
int Run(void* p) {
    auto* s = static_cast<WorkerCall::State*>(p);
    try { s->fn(); } catch (...) {}   // a throwing call still finishes: the editor must not wait for it forever
    s->fn = nullptr;                  // what it captured goes on this thread
    if (s->state.exchange(1) == 2) delete s;
    return 0;
}
}  // namespace

bool WorkerCall::Start(std::function<void()> fn, const char* name) {
    if (m_state || !fn) return false;
    State* s = new State();
    s->fn = std::move(fn);
    okay::thr::Thread* t = okay::thr::CreateThread(&Run, name, s);
    if (!t) {   // no thread: run it here
        Run(s);   // state becomes 1 (finished)
        m_state = s;
        m_thread = nullptr;
        return true;
    }
    m_state = s;
    m_thread = t;
    return true;
}

bool WorkerCall::Collect() {
    if (!m_state || m_state->state.load() != 1) return false;
    if (m_thread) okay::thr::WaitThread(m_thread, nullptr);   // it has returned: this only collects the thread record
    m_thread = nullptr;
    delete m_state;
    m_state = nullptr;
    return true;
}

void WorkerCall::Drop() {
    if (!m_state) return;
    if (m_state->state.exchange(2) == 1) delete m_state;   // finished already: ours to free; otherwise the thread frees it
    if (m_thread) okay::thr::DetachThread(m_thread);
    m_state = nullptr;
    m_thread = nullptr;
}

}  // namespace oked
