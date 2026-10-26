#pragma once
// ---------------------------------------------------------------------------
// PlayerInput / IPlayerSim — what a player asked for this tick, and the
// controllers that can be driven by it (server-authoritative movement, net
// plan 5b).
//
// A controller that implements IPlayerSim reads its local input in ONE place
// (ReadLocalInput) and moves only through Simulate(input, dt), which reads no
// Input:: globals. Local play does exactly that every frame, so it feels the
// same; a server with NetworkManager::authoritativeMovement runs the same
// Simulate with the commands a client sent, at its fixed tick.
// ---------------------------------------------------------------------------
#include "okay/Math/Vec2.hpp"
#include "okay/Scene/GameObject.hpp"
#include <cmath>
#include <cstdint>

namespace okay {

struct PlayerInput {
    enum Button : std::uint32_t {
        Jump = 1u << 0, Sprint = 1u << 1, Crouch = 1u << 2, Prone = 1u << 3,
        Fire = 1u << 4, AltFire = 1u << 5, Use = 1u << 6, Reload = 1u << 7,
        LeanLeft = 1u << 8, LeanRight = 1u << 9,
        StanceButton = 1u << 10,   ///< the pad's stance button (tap crouch / stand, hold prone)
        Dash = 1u << 11,
        Custom0 = 1u << 16,   ///< bits 16..31: the game's own buttons
    };
    Vec2 move{0.0f, 0.0f};      ///< x = strafe (right +), y = forward; length <= 1 after Clamp
    float yaw = 0.0f;           ///< look direction, degrees
    float pitch = 0.0f;         ///< degrees, -89..89 after Clamp
    std::uint32_t held = 0;     ///< buttons down now
    std::uint32_t pressed = 0;  ///< buttons that went down since the previous command

    bool Held(std::uint32_t b) const { return (held & b) != 0; }
    bool Pressed(std::uint32_t b) const { return (pressed & b) != 0; }

    /// What a server accepts: finite, a move no longer than 1, pitch within
    /// +-89 and yaw within -180..180. (A press counts once per command, so a
    /// button can't be pressed faster than the tick.)
    void Clamp() {
        if (!std::isfinite(move.x) || !std::isfinite(move.y)) move = Vec2{0.0f, 0.0f};
        const float len = std::sqrt(move.x * move.x + move.y * move.y);
        if (len > 1.0f) { move.x /= len; move.y /= len; }
        if (!std::isfinite(yaw)) yaw = 0.0f;
        yaw = std::remainder(yaw, 360.0f);
        if (!std::isfinite(pitch)) pitch = 0.0f;
        pitch = pitch < -89.0f ? -89.0f : (pitch > 89.0f ? 89.0f : pitch);
    }
};

/// A controller that can be driven by PlayerInput (see the file comment).
class IPlayerSim {
public:
    virtual ~IPlayerSim() = default;
    /// This frame's local input (keyboard, mouse, pad), as the controller reads it.
    virtual PlayerInput ReadLocalInput() = 0;
    /// Move by `in` for `dt` seconds. Reads no Input:: globals.
    virtual void Simulate(const PlayerInput& in, float dt) = 0;
};

/// The first IPlayerSim on `go`, or null.
inline IPlayerSim* FindPlayerSim(GameObject* go) {
    if (!go) return nullptr;
    for (const auto& c : go->Components())
        if (auto* s = dynamic_cast<IPlayerSim*>(c.get())) return s;
    return nullptr;
}

} // namespace okay
