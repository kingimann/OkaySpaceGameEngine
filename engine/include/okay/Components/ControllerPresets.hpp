#pragma once
// ---------------------------------------------------------------------------
// Controller feel presets: one pick sets the whole "how does it move" block of a First or Third Person
// Controller, so the owner can compare feels side by side (Inspector combo, the F3 player line, and the
// number keys 1-5 in examples/player/player_feel_lab.okayscene).
//
//   Classic    the shipped defaults (what a new controller has). Applying it restores them.
//   Modern     quick but not twitchy, variable jump height, a light head bob.
//   Arcade     very snappy: near-instant start / stop / turn, full air control, double jump, forgiving timing.
//   Realistic  weighty: slow to build speed and to stop, little air control, real jump height, firmer bob.
//   Shooter    fast and flat: high ground accel, short stop, modest air control, minimal bob, wide sprint FOV.
//
// A preset only sets values that already exist as settings (speeds, acceleration, deceleration, steer rate,
// air control, coyote time, jump buffer, jumps, variable jump, look smoothing, FOV kick, landing dip, the
// first-person head bob and the third-person camera follow). Nothing about the simulation changes, so a
// networked player keeps predicting the same way as long as host and client use the same preset (the
// numbers travel with the scene). `controllerPreset` remembers the pick (saved with the scene); a later
// hand edit does not change it. The numbers were chosen by feel and measured by tests/test_player_feel.cpp
// (time to full speed, stopping distance, air steering). Written from the settings' own meaning; no other
// project's tuning was used.
// ---------------------------------------------------------------------------
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include <cstring>

namespace okay {

struct ControllerPresets {
    enum Id { Classic = 0, Modern = 1, Arcade = 2, Realistic = 3, Shooter = 4, Count = 5 };

    static const char* Name(int id) {
        static const char* kNames[Count] = {"Classic", "Modern", "Arcade", "Realistic", "Shooter"};
        return id >= 0 && id < Count ? kNames[id] : "Custom";
    }
    /// A one-line description for a tooltip.
    static const char* About(int id) {
        switch (id) {
            case Classic:   return "The shipped defaults.";
            case Modern:    return "Quick and smooth, variable jump height, light head bob.";
            case Arcade:    return "Snappy: near-instant start, stop and turn, full air control, double jump.";
            case Realistic: return "Weighty: slow to start and stop, little air control, firm bob.";
            case Shooter:   return "Fast and flat: high acceleration, short stop, minimal bob, wide sprint FOV.";
            default:        return "Hand-tuned.";
        }
    }
    /// A pick from text (any case): "arcade" -> 2, unknown -> -1.
    static int FromName(const char* name) {
        if (!name) return -1;
        for (int i = 0; i < Count; ++i) {
            const char* a = Name(i); const char* b = name; bool same = true;
            for (; *a || *b; ++a, ++b) {
                const char ca = (char)(*a >= 'A' && *a <= 'Z' ? *a + 32 : *a);
                const char cb = (char)(*b >= 'A' && *b <= 'Z' ? *b + 32 : *b);
                if (ca != cb) { same = false; break; }
            }
            if (same) return i;
        }
        return -1;
    }

    /// The movement block, shared by both controllers (same field names).
    struct Move {
        float walkSpeed, runSpeed, jumpForce, walkAcceleration, acceleration, deceleration, steerRate, airControl;
        float coyoteTime, jumpBufferTime; int maxJumps; bool variableJump; float jumpCut;
        float lookSmoothing, sprintFovBoost, fovLerp, stanceLerp, hardLandSpeed;
    };
    /// The first-person-only block: bob, sway, landing dip, lean.
    struct Eye { bool headBob; float bobAmount, bobSway, bobRunScale, bobBlend, landDip; };
    /// The third-person-only block: how the character turns and how the camera follows.
    struct Follow { float turnSpeed, pivotDampingXZ, pivotDampingY, orbitSmoothing, cameraCollisionPullIn, cameraCollisionRecover; };

    template <class C>
    static Move MoveOf(int id) {
        switch (id) {
            case Modern:    return {2.0f, 5.5f, 6.0f,  70.0f,  110.0f,  90.0f, 260.0f, 0.60f, 0.12f, 0.14f, 1, true, 0.50f, 0.0f, 8.0f, 8.0f, 12.0f, 14.0f};
            case Arcade:    return {2.6f, 7.0f, 7.5f, 140.0f,  220.0f, 200.0f, 480.0f, 1.00f, 0.20f, 0.20f, 2, true, 0.45f, 0.0f, 12.0f, 10.0f, 16.0f, 20.0f};
            case Realistic: return {1.8f, 5.0f, 5.0f,  26.0f,   42.0f,  34.0f,  80.0f, 0.12f, 0.06f, 0.08f, 1, false, 0.50f, 0.04f, 5.0f, 6.0f, 8.0f, 9.0f};
            case Shooter:   return {3.2f, 6.2f, 6.0f, 150.0f,  190.0f, 150.0f, 400.0f, 0.35f, 0.10f, 0.12f, 1, false, 0.50f, 0.0f, 10.0f, 10.0f, 14.0f, 12.0f};
            default: break;
        }
        // Classic: whatever a new controller starts with, read from one so it can never drift from the defaults.
        C d;
        return {d.walkSpeed, d.runSpeed, d.jumpForce, d.walkAcceleration, d.acceleration, d.deceleration, d.steerRate, d.airControl,
                d.coyoteTime, d.jumpBufferTime, d.maxJumps, d.variableJump, d.jumpCut,
                d.lookSmoothing, d.sprintFovBoost, d.fovLerp, d.stanceLerp, d.hardLandSpeed};
    }
    static Eye EyeOf(int id) {
        switch (id) {
            case Modern:    return {true, 0.030f, 0.0f, 0.8f, 8.0f, 0.10f};
            case Arcade:    return {true, 0.020f, 0.0f, 0.6f, 12.0f, 0.06f};
            case Realistic: return {true, 0.045f, 0.9f, 1.0f, 6.0f, 0.18f};
            case Shooter:   return {true, 0.018f, 0.0f, 0.7f, 10.0f, 0.08f};
            default: break;
        }
        FirstPersonController d;
        return {d.headBob, d.bobAmount, d.bobSway, d.bobRunScale, d.bobBlend, d.landDip};
    }
    static Follow FollowOf(int id) {
        switch (id) {
            case Modern:    return {24.0f, 20.0f, 10.0f, 0.04f, 30.0f, 6.0f};
            case Arcade:    return {40.0f, 26.0f, 14.0f, 0.02f, 40.0f, 9.0f};
            case Realistic: return {10.0f, 14.0f,  7.0f, 0.08f, 24.0f, 4.0f};
            case Shooter:   return {32.0f, 24.0f, 12.0f, 0.02f, 40.0f, 8.0f};
            default: break;
        }
        ThirdPersonController d;
        return {d.turnSpeed, d.pivotDampingXZ, d.pivotDampingY, d.orbitSmoothing, d.cameraCollisionPullIn, d.cameraCollisionRecover};
    }

    template <class C>
    static void ApplyMove(C& c, const Move& m) {
        c.walkSpeed = m.walkSpeed; c.runSpeed = m.runSpeed; c.jumpForce = m.jumpForce;
        c.walkAcceleration = m.walkAcceleration; c.acceleration = m.acceleration; c.deceleration = m.deceleration;
        c.steerRate = m.steerRate; c.airControl = m.airControl;
        c.coyoteTime = m.coyoteTime; c.jumpBufferTime = m.jumpBufferTime; c.maxJumps = m.maxJumps;
        c.variableJump = m.variableJump; c.jumpCut = m.jumpCut;
        c.lookSmoothing = m.lookSmoothing; c.sprintFovBoost = m.sprintFovBoost; c.fovLerp = m.fovLerp;
        c.stanceLerp = m.stanceLerp; c.hardLandSpeed = m.hardLandSpeed;
    }

    /// Set the feel block of a controller. Returns false for an unknown id (nothing changed).
    static bool Apply(FirstPersonController& c, int id) {
        if (id < 0 || id >= Count) return false;
        ApplyMove(c, MoveOf<FirstPersonController>(id));
        const Eye e = EyeOf(id);
        c.headBob = e.headBob; c.bobAmount = e.bobAmount; c.bobSway = e.bobSway; c.bobRunScale = e.bobRunScale;
        c.bobBlend = e.bobBlend; c.landDip = e.landDip;
        c.controllerPreset = id;
        return true;
    }
    static bool Apply(ThirdPersonController& c, int id) {
        if (id < 0 || id >= Count) return false;
        ApplyMove(c, MoveOf<ThirdPersonController>(id));
        const Follow f = FollowOf(id);
        c.turnSpeed = f.turnSpeed; c.pivotDampingXZ = f.pivotDampingXZ; c.pivotDampingY = f.pivotDampingY;
        c.orbitSmoothing = f.orbitSmoothing; c.cameraCollisionPullIn = f.cameraCollisionPullIn;
        c.cameraCollisionRecover = f.cameraCollisionRecover;
        c.controllerPreset = id;
        return true;
    }
    /// Apply to whichever controllers the object has (first and third person both, when it carries both).
    static bool ApplyTo(GameObject* go, int id) {
        if (!go || id < 0 || id >= Count) return false;
        bool any = false;
        for (auto* fp : go->GetComponents<FirstPersonController>()) any |= Apply(*fp, id);
        for (auto* tp : go->GetComponents<ThirdPersonController>()) any |= Apply(*tp, id);
        return any;
    }
    /// The next preset after `id` (wraps), for a "cycle" key.
    static int Next(int id) { return (id + 1) % Count; }
};

}  // namespace okay
