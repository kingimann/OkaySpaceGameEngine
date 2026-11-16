#pragma once
// ---------------------------------------------------------------------------
// PlayerDebug: the player's own numbers as text lines, for the F3 panel of the player and the stats overlay of
// the editor's Game view. For the player this peer plays (a first- or third-person controller that simulated):
// ground or air, how the ground was found, the slope under the feet against the limit, speed, velocity, the
// surface normal, what is stood on, stance, water. Reads the controllers' last step only; changes nothing.
// Written from scratch for this engine.
// ---------------------------------------------------------------------------
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/PlayerPerf.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Scene/Scene.hpp"
#include <cstdio>
#include <string>
#include <vector>

namespace okay {

class PlayerDebug {
public:
    /// Lines for the local player of `scene`; empty when there is none (or it has not simulated yet).
    static std::vector<std::string> Lines(const Scene& scene) {
        std::vector<std::string> out;
        for (const auto& up : scene.Objects()) {
            GameObject* go = up.get();
            if (!go || !go->ActiveInHierarchy() || !IsLocallyControlled(go)) continue;
            const PlayerDebugState* st = nullptr;
            const char* view = "";
            if (auto* fp = go->GetComponent<FirstPersonController>(); fp && fp->SimActive()) { st = &fp->debugState(); view = "first person"; }
            else if (auto* tp = go->GetComponent<ThirdPersonController>(); tp && tp->SimActive()) { st = &tp->debugState(); view = "third person"; }
            if (!st || !st->valid) continue;
            Format(*st, go->name, view, out);
            break;   // one local player
        }
        return out;
    }

    /// The text for one state (public so a test can pin it).
    static void Format(const PlayerDebugState& s, const std::string& name, const char* view, std::vector<std::string>& out) {
        char b[200];
        std::snprintf(b, sizeof b, "Player '%s' (%s)%s", name.c_str(), view, OldPlayerPath() ? "  [old player path]" : "");
        out.emplace_back(b);
        const char* ground = s.climbing ? "climbing" : s.swimming ? "swimming" : s.grounded ? (s.terrainGround ? "grounded (terrain)" : "grounded") : "in the air";
        std::snprintf(b, sizeof b, "  %s%s%s   stance %s", ground, s.held ? ", held on slope" : "", s.moving ? ", moving" : "",
                      s.stance == 0 ? "stand" : s.stance == 1 ? "crouch" : "prone");
        out.emplace_back(b);
        std::snprintf(b, sizeof b, "  slope %.1f deg (limit %.0f)%s   speed %.2f m/s", s.slopeDeg, s.slopeLimit,
                      s.slopeLimit > 0.0f && s.slopeDeg > s.slopeLimit ? " TOO STEEP" : "", s.speed);
        out.emplace_back(b);
        std::snprintf(b, sizeof b, "  velocity (%.2f, %.2f, %.2f)   ground normal (%.2f, %.2f, %.2f)", s.velocity.x, s.velocity.y,
                      s.velocity.z, s.groundNormal.x, s.groundNormal.y, s.groundNormal.z);
        out.emplace_back(b);
        if (s.lastLandSpeed > 0.0f) {
            std::snprintf(b, sizeof b, "  last landing %.1f m/s, hard landings %d", s.lastLandSpeed, s.hardLandings);
            out.emplace_back(b);
        }
        if (s.platform) {
            std::snprintf(b, sizeof b, "  standing on '%s'%s", s.platform->name.c_str(), s.platformMoves ? " (moving)" : "");
            out.emplace_back(b);
        }
    }
};

/// What the Scene view draws around a selected player: a ring at the feet, four spokes that rise at the Slope Limit
/// (the steepest ground it may stand on), and a ring at the Step Offset height. Lines only; the editor draws them.
struct PlayerGizmo {
    struct Seg { Vec3 a, b; int kind; };   // kind 0 ring, 1 slope spoke, 2 step ring
    static std::vector<Seg> Lines(GameObject* go) {
        std::vector<Seg> out;
        if (!go || !go->transform) return out;
        float slope = 0.0f, step = 0.0f; bool have = false;
        if (auto* fp = go->GetComponent<FirstPersonController>()) { slope = fp->slopeLimit; step = fp->stepOffset; have = true; }
        else if (auto* tp = go->GetComponent<ThirdPersonController>()) { slope = tp->slopeLimit; step = tp->stepOffset; have = true; }
        if (!have) return out;
        const Vec3 o = go->transform->Position();
        auto ring = [&](float r, float y, int kind) {
            for (int i = 0; i < 24; ++i) {
                const float a0 = i * 0.2617994f, a1 = (i + 1) * 0.2617994f;
                out.push_back({o + Vec3{std::cos(a0) * r, y, std::sin(a0) * r}, o + Vec3{std::cos(a1) * r, y, std::sin(a1) * r}, kind});
            }
        };
        ring(0.45f, 0.02f, 0);
        if (step > 0.0f) ring(0.45f, step, 2);
        if (slope > 0.0f && slope < 90.0f) {
            const float th = slope * 0.0174532925f;
            const Vec3 d[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
            for (const Vec3& dir : d) out.push_back({o + dir * 0.45f + Vec3{0, 0.02f, 0}, o + dir * (0.45f + 1.2f * std::cos(th)) + Vec3{0, 0.02f + 1.2f * std::sin(th), 0}, 1});
        }
        return out;
    }
};

} // namespace okay
