#pragma once
// ---------------------------------------------------------------------------
// StanceControl — crouch and prone for a movement controller: the keys (or a
// script / an NPC's SetStance), the stance the body settles into (rising needs
// headroom), and the eased stance level the collider, the speed cap and the
// Animator follow over the body's stance change times (see Stance.hpp).
// Deterministic: the keys and dt only.
// ---------------------------------------------------------------------------
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/StepUp.hpp"
#include "okay/Input/Input.hpp"
#include <algorithm>

namespace okay {

struct StanceControl {
    StanceControl() = default;
    StanceControl(char crouch, char prone, bool proneAllowed)
        : crouchKey(crouch), proneKey(prone), allowProne(proneAllowed) {}

    // ---- Settings (shown and saved with the controller) ----
    char  crouchKey = Input::KeyCtrl;   ///< 0 = no key (SetStance only)
    char  proneKey = 'z';               ///< 0 = no key
    bool  toggle = true;                ///< tap to toggle vs hold
    bool  allowProne = true;
    bool  resizeBody = true;            ///< crouch / prone shrink the collider (fit under low things)
    float crouchSpeed = 2.2f, proneSpeed = 1.1f;
    float crouchHeight = 0.65f, proneHeight = 0.33f;   ///< fractions of the standing body

    /// Ask for a stance (0 stand, 1 crouch, 2 prone): a script, an NPC, a cutscene.
    /// It's kept until changed, so a blocked stand-up happens once there's room.
    void Set(int s) { m_want = std::clamp(s, 0, 2); }
    int   Want() const { return m_want; }
    int   Stance() const { return m_stance; }   ///< the stance the body is in (or getting into)
    float Level() const { return m_level; }     ///< on the way: 0 stand .. 1 crouch .. 2 prone
    const StanceBody& Body() const { return m_body; }

    /// This frame's keys: a tap toggles (or, in hold mode, the held key decides).
    void Keys(bool crouchDown, bool crouchHeld, bool proneDown, bool proneHeld) {
        if (toggle) {
            if (crouchDown) m_want = m_want == 1 ? 0 : 1;
            if (proneDown)  m_want = m_want == 2 ? 0 : 2;
        } else {
            m_want = proneHeld ? 2 : (crouchHeld ? 1 : 0);
        }
    }
    /// Keys() from the keyboard (for controllers that read input in Update).
    void ReadKeys() {
        if (!crouchKey && !proneKey) return;
        Keys(crouchKey && Input::GetKeyDown(crouchKey), crouchKey && Input::GetKey(crouchKey),
             proneKey && Input::GetKeyDown(proneKey), proneKey && Input::GetKey(proneKey));
    }
    /// Settle the stance asked for (headroom permitting) and ease the body there.
    void Step(GameObject* go, float dt) {
        if (!allowProne && m_want == 2) m_want = 0;
        m_stance = resizeBody && go ? SettleStance(go, m_body, m_stance, m_want, crouchHeight, proneHeight) : m_want;
        EaseStance(go, m_body, m_level, m_stance, dt, StanceTimesOf(go), resizeBody, crouchHeight, proneHeight);
    }
    /// The speed cap for the body now, given the standing one.
    float Speed(float standing) const { return StanceValue(m_level, standing, crouchSpeed, proneSpeed); }
    /// Begin crouched / prone (a scene saved that way); Start() sizes the body.
    void Restore(int s) { m_want = m_stance = std::clamp(s, 0, 2); m_level = (float)m_stance; }
    /// From the controller's Start: size a restored crouched / prone body before physics runs.
    void Start(GameObject* go) {
        if (!resizeBody || !go || m_stance == 0) return;
        CaptureStance(go, m_body);
        ApplyStanceHeight(go, m_body, m_body.standH * StanceValue(m_level, 1.0f, crouchHeight, proneHeight));
    }

private:
    int   m_want = 0, m_stance = 0;
    float m_level = 0.0f;
    StanceBody m_body;
};

} // namespace okay
