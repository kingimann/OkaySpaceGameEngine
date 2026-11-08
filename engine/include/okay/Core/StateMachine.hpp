#pragma once
#include <functional>
#include <map>
#include <set>
#include <utility>

namespace okay {

/// A small generic state machine: a state is any value with operator< (an enum, an
/// int, a string). Each state can have enter / update / exit callbacks, and
/// transitions can optionally be restricted with Allow().
///
///     enum class Door { Closed, Open };
///     StateMachine<Door> sm;
///     sm.Add(Door::Open, {[]{ /*enter*/ }, [](float dt){ /*update*/ }, []{ /*exit*/ }});
///     sm.Start(Door::Closed);
///     sm.Change(Door::Open);
///     sm.Update(dt);
///
/// Changing state from inside a callback is allowed: the machine is already in the
/// new state when its enter callback runs, and a Change made from an enter callback
/// is carried out after that callback returns.
template <typename State>
class StateMachine {
public:
    struct Handlers {
        std::function<void()>      onEnter;
        std::function<void(float)> onUpdate;
        std::function<void()>      onExit;
    };

    /// Register (or replace) the callbacks of a state. A state needs no entry to be used.
    void Add(const State& s, Handlers h) { m_handlers[s] = std::move(h); }

    /// Restrict transitions: once any Allow(from, ...) exists, a change out of `from` is
    /// only accepted to a state listed for it. States with no Allow are unrestricted.
    void Allow(const State& from, const State& to) { m_allowed[from].insert(to); }

    /// Called after every accepted change as (from, to), once the new state is entered.
    void OnTransition(std::function<void(const State&, const State&)> fn) { m_onTransition = std::move(fn); }

    /// Enter the first state (no exit callback, no transition rule). Re-starting exits the old one.
    void Start(const State& s) {
        if (m_has) Exit();
        m_has = true; m_current = s; m_previous = s; m_time = 0.0f;
        Enter();
    }
    /// Leave the current state (runs its exit callback) and become empty.
    void Stop() { if (m_has) { Exit(); m_has = false; } }

    /// Whether a change from the current state to `to` would be accepted.
    bool CanChange(const State& to) const {
        if (!m_has) return true;
        auto it = m_allowed.find(m_current);
        return it == m_allowed.end() || it->second.count(to) > 0;
    }

    /// Exit the current state and enter `to`. False (nothing runs) when already in `to`
    /// or when the transition is not allowed. With no current state it acts like Start.
    bool Change(const State& to) {
        if (!m_has) { Start(to); return true; }
        if (!(m_current < to) && !(to < m_current)) return false;
        if (!CanChange(to)) return false;
        if (m_busy) { m_pending = true; m_pendingTo = to; return true; }   // from inside an enter callback
        const State from = m_current;
        Exit();
        m_previous = from; m_current = to; m_time = 0.0f;
        Enter();
        if (m_onTransition) m_onTransition(from, to);
        return true;
    }

    /// Run the current state's update callback and advance TimeInState.
    void Update(float dt) {
        if (!m_has) return;
        m_time += dt;
        auto it = m_handlers.find(m_current);
        if (it != m_handlers.end() && it->second.onUpdate) it->second.onUpdate(dt);
    }

    bool         HasState() const { return m_has; }
    const State& Current()  const { return m_current; }
    const State& Previous() const { return m_previous; }   ///< the state before the last change (= Current at Start)
    bool Is(const State& s) const { return m_has && !(m_current < s) && !(s < m_current); }
    /// Seconds (sum of Update dt) spent in the current state.
    float TimeInState() const { return m_time; }

private:
    void Enter() {
        auto it = m_handlers.find(m_current);
        if (it == m_handlers.end() || !it->second.onEnter) return;
        m_busy = true;
        it->second.onEnter();
        m_busy = false;
        if (m_pending) { m_pending = false; Change(m_pendingTo); }
    }
    void Exit() {
        auto it = m_handlers.find(m_current);
        if (it != m_handlers.end() && it->second.onExit) it->second.onExit();
    }

    std::map<State, Handlers> m_handlers;
    std::map<State, std::set<State>> m_allowed;
    std::function<void(const State&, const State&)> m_onTransition;
    State m_current{}, m_previous{}, m_pendingTo{};
    bool  m_has = false, m_busy = false, m_pending = false;
    float m_time = 0.0f;
};

} // namespace okay
