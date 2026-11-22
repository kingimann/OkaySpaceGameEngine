#pragma once
// Two small helpers for gameplay timing (header-only, no clock of their own: the game passes the frame time).
//  * InputBuffer: remember a press for a moment so "jump pressed just before landing" still jumps.
//  * CoyoteTime: keep "can jump" true for a moment after walking off a ledge.
// Written from the general idea; see tests/test_action_phases.cpp.
namespace okay {

class InputBuffer {
public:
    /// The player pressed the button now.
    void Press() { m_since = 0.0f; m_has = true; }
    void Update(float dt) { if (m_has) m_since += dt; }
    /// A press happened within `window` seconds and has not been used.
    bool Pending(float window = 0.15f) const { return m_has && m_since <= window; }
    /// Use the press: true once per press.
    bool Consume(float window = 0.15f) { if (!Pending(window)) return false; m_has = false; return true; }
    void Clear() { m_has = false; }
private:
    float m_since = 0.0f; bool m_has = false;
};

class CoyoteTime {
public:
    /// Call once a frame with whether the character is on the ground.
    void Update(bool grounded, float dt) {
        if (grounded) { m_off = 0.0f; m_used = false; m_grounded = true; }
        else { m_grounded = false; m_off += dt; }
    }
    /// Grounded, or off the ground for no longer than `window` seconds and not used since.
    bool CanUse(float window = 0.1f) const { return m_grounded || (!m_used && m_off <= window); }
    /// The jump happened: no second one in the grace time.
    void Use() { m_used = true; }
    float TimeOffGround() const { return m_off; }
private:
    float m_off = 0.0f; bool m_used = false, m_grounded = true;
};

} // namespace okay
