#pragma once
// Runs the game's tick on its own stack, on the editor's own thread, so a
// visual-script breakpoint can freeze the game in the middle of a frame: the
// breakpoint handler calls Yield(), the editor carries on drawing (the game
// frozen exactly where it stopped), and Resume() later continues the tick from
// that point. Staying on one thread keeps OpenGL, audio and thread-locals happy.
// Only used while debugging (a breakpoint is set); otherwise the editor calls
// the tick directly.
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <ucontext.h>
#endif

namespace okay::editor {

class GameFiber {
public:
    static constexpr std::size_t kStackSize = 8u * 1024u * 1024u;   // as much as a normal thread gets

    GameFiber() = default;
    GameFiber(const GameFiber&) = delete;
    GameFiber& operator=(const GameFiber&) = delete;
    ~GameFiber() {
#ifdef _WIN32
        if (m_fiber) DeleteFiber(m_fiber);
#endif
    }

    /// Run `body` on the fiber. Returns when it finished or called Yield().
    /// Must not be called while a previous body is suspended (use Resume()).
    void Run(std::function<void()> body) {
        if (m_suspended) return;
        m_body = std::move(body);
        m_done = false;
#ifdef _WIN32
        if (!m_main) m_main = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
        if (!m_fiber) m_fiber = CreateFiber(kStackSize, &GameFiber::Proc, this);
        if (!m_main || !m_fiber) { m_body(); m_done = true; return; }   // no fibers: just run it
#else
        if (m_stack.empty()) m_stack.resize(kStackSize);
        getcontext(&m_fib);
        m_fib.uc_stack.ss_sp = m_stack.data();
        m_fib.uc_stack.ss_size = m_stack.size();
        m_fib.uc_link = &m_main;
        const auto p = reinterpret_cast<std::uintptr_t>(this);
        makecontext(&m_fib, reinterpret_cast<void (*)()>(&GameFiber::Trampoline), 2,
                    static_cast<unsigned>(p & 0xffffffffu), static_cast<unsigned>((std::uint64_t)p >> 32));
#endif
        Enter();
    }
    /// Carry on with a suspended body until it finishes or yields again.
    void Resume() { if (m_suspended) Enter(); }
    /// From inside the body: freeze here and return to Run()/Resume()'s caller.
    void Yield() {
        if (!m_inFiber) return;
        m_suspended = true;
        m_inFiber = false;
#ifdef _WIN32
        SwitchToFiber(m_main);
#else
        swapcontext(&m_fib, &m_main);
#endif
        m_inFiber = true;
        m_suspended = false;
    }
    /// True while code is running on the fiber (inside the body).
    bool InFiber() const { return m_inFiber; }
    /// True while a body has yielded and not finished: the game is frozen mid-tick.
    bool Suspended() const { return m_suspended; }

private:
    std::function<void()> m_body;
    bool m_inFiber = false, m_suspended = false, m_done = true;

    void Enter() {
        m_inFiber = true;
#ifdef _WIN32
        SwitchToFiber(m_fiber);
#else
        swapcontext(&m_main, &m_fib);
#endif
        m_inFiber = false;
    }
    void RunBody() {
        // Nothing may unwind past the fiber's first frame: there's no caller there.
        try { if (m_body) m_body(); }
        catch (const std::exception& e) { std::fprintf(stderr, "Game tick threw: %s\n", e.what()); }
        catch (...) { std::fprintf(stderr, "Game tick threw an unknown exception\n"); }
        m_done = true;
        m_body = nullptr;
    }
#ifdef _WIN32
    void* m_main = nullptr;
    void* m_fiber = nullptr;
    static void CALLBACK Proc(void* self) {
        auto* f = static_cast<GameFiber*>(self);
        for (;;) {   // one fiber reused for every tick
            f->RunBody();
            f->m_inFiber = false;
            SwitchToFiber(f->m_main);
        }
    }
#else
    ucontext_t m_main{}, m_fib{};
    std::vector<char> m_stack;
    static void Trampoline(unsigned lo, unsigned hi) {
        auto* f = reinterpret_cast<GameFiber*>(static_cast<std::uintptr_t>(lo) | (static_cast<std::uintptr_t>(hi) << 32));
        f->RunBody();
        // Returning resumes uc_link (the editor).
    }
#endif
};

} // namespace okay::editor
