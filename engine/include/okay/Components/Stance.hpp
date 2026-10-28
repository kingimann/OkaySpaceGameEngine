#pragma once
// ---------------------------------------------------------------------------
// Stance — how a body moves between standing, crouching and lying prone.
// One stance level eases from 0 (stand) through 1 (crouch) to 2 (prone) over
// the transition times. The collider height, the camera drop, the speed cap and
// the Animator's `stance` parameter all follow it, so a change of mind halfway
// carries on from wherever the body is. Pure functions of the input and dt
// (movement runs in Simulate, on the server and in client prediction).
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cmath>

namespace okay {

/// Seconds each stance change takes (the Animator stores a scene's own times).
struct StanceTimes {
    float standCrouch = 0.25f;   ///< stand <-> crouch
    float crouchProne = 0.5f;    ///< crouch <-> prone
    float proneStand  = 0.8f;    ///< prone -> stand, all the way (getting up through a kneel)
};

/// Seconds for the locomotion crossfades (the Animator stores a scene's own).
/// Both directions of a pair share a time. Stance changes stay on StanceTimes.
struct GaitCrossfades {
    float idleWalk  = 0.28f;   ///< idle <-> walk
    float walkRun   = 0.22f;   ///< walk <-> run, and idle <-> run
    float groundAir = 0.12f;   ///< into a jump or a fall
    float airGround = 0.24f;   ///< back onto the ground, including a landing
};

/// The level `dt` seconds on from `level`, heading for stance `target`
/// (0 stand, 1 crouch, 2 prone). Each half of the way takes its pair's time;
/// rising from prone to stand takes proneStand in total.
inline float StepStanceLevel(float level, int target, float dt, const StanceTimes& t) {
    const float goal = (float)std::clamp(target, 0, 2);
    float left = dt;
    while (left > 0.0f && level != goal) {
        const bool down = goal > level;
        const bool upper = down ? level >= 1.0f : level > 1.0f;   // on the crouch..prone half
        float secs = upper ? t.crouchProne : t.standCrouch;
        if (upper && !down && goal == 0.0f)   // getting all the way up: the rise from prone fits proneStand
            secs = t.proneStand - t.standCrouch;
        secs = std::max(secs, 1e-3f);
        const float edge = down ? std::min(upper ? 2.0f : 1.0f, goal) : std::max(upper ? 1.0f : 0.0f, goal);
        const float need = std::fabs(edge - level) * secs;
        if (need <= left) { level = edge; left -= need; }
        else { level += (down ? left : -left) / secs; left = 0.0f; }
    }
    return level;
}

/// A value at stance `level`, eased in and out on each half of the way
/// (stand = a, crouch = b, prone = c).
inline float StanceValue(float level, float a, float b, float c) {
    level = std::clamp(level, 0.0f, 2.0f);
    const bool upper = level > 1.0f;
    float u = upper ? level - 1.0f : level;
    u = u * u * (3.0f - 2.0f * u);
    return upper ? b + (c - b) * u : a + (b - a) * u;
}

} // namespace okay
