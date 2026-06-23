#pragma once

namespace okay {

/// Mouse-cursor control, mirroring Unity's `Cursor`. Gameplay sets the desired
/// state (e.g. lock + hide for a shooter); the host (player / editor) reads it each
/// frame and applies it to the window. Scripts and controllers just set the fields.
class Cursor {
public:
    enum class LockMode { None, Locked };   // Unity also has Confined; Locked covers FPS/TPS

    /// Hide/show the hardware cursor (Unity's Cursor.visible).
    static inline bool visible = true;
    /// None = free cursor; Locked = cursor hidden-centered, motion delivered as
    /// relative deltas so mouse-look keeps working (Unity's Cursor.lockState).
    static inline LockMode lockState = LockMode::None;

    static void SetVisible(bool v) { visible = v; }
    static void SetLock(LockMode m) { lockState = m; }
    /// Convenience: lock + hide (typical first/third-person shooter), or free + show.
    static void Capture(bool on) {
        lockState = on ? LockMode::Locked : LockMode::None;
        visible   = !on;
    }
    static bool IsLocked() { return lockState == LockMode::Locked; }
};

} // namespace okay
