#pragma once
#include "okay/Math/Curves.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>
#include <vector>
#include <string>

namespace okay {

/// A smooth camera rail — Cinemachine's CinemachinePath/SmoothPath. Holds an
/// ordered list of waypoints (in the path GameObject's local space) and evaluates
/// a Catmull-Rom spline through them, so a Tracked Dolly virtual camera or a Dolly
/// Cart can glide along a curved track. Optionally looped into a closed loop.
class DollyPath : public Behaviour {
public:
    /// Waypoints in local space (transformed by this GameObject's Transform).
    std::vector<Vec3> waypoints;
    /// Join the last waypoint back to the first into a closed loop.
    bool looped = false;

    int Count() const { return (int)waypoints.size(); }

    /// Map a raw path position to the valid domain: wrap when looped, else clamp.
    float Normalize(float t) const {
        if (looped) { t -= std::floor(t); return t; }
        return Mathf::Clamp01(t);
    }

    /// World-space point at normalized position t in [0,1] along the whole path.
    Vec3 EvaluatePosition(float t) const {
        int n = (int)waypoints.size();
        if (n == 0) return transform ? transform->Position() : Vec3::Zero;
        if (n == 1) return ToWorld(waypoints[0]);

        int segs = looped ? n : (n - 1);
        t = Normalize(t);
        float ft = t * segs;
        int i = (int)ft;
        if (i >= segs) i = segs - 1;
        float u = ft - i;

        Vec3 local = CatmullRom(P(i - 1), P(i), P(i + 1), P(i + 2), u);
        return ToWorld(local);
    }

    /// The normalized position whose world point is closest to `world` (coarse scan
    /// + local refine). Used by auto-dolly to track a moving subject along the rail.
    float NearestPosition(const Vec3& world, int samples = 128) const {
        if (waypoints.size() < 2) return 0.0f;
        float best = 0.0f, bestD = 1e30f;
        for (int k = 0; k <= samples; ++k) {
            float t = (float)k / samples;
            float d = (EvaluatePosition(t) - world).SqrMagnitude();
            if (d < bestD) { bestD = d; best = t; }
        }
        return best;
    }

    /// World-space direction of travel at t (unit length; zero for a path with fewer than two distinct points).
    /// Taken from a short step along the curve, so it follows the object's scale and rotation too.
    Vec3 EvaluateTangent(float t) const {
        if (waypoints.size() < 2) return Vec3::Zero;
        const float h = 1e-3f;
        float a = t - h, b = t + h;
        if (!looped) { a = Mathf::Clamp01(a); b = Mathf::Clamp01(b); }
        Vec3 d = EvaluatePosition(b) - EvaluatePosition(a);
        float m = d.Magnitude();
        return m > 1e-7f ? d * (1.0f / m) : Vec3::Zero;
    }

    /// World length of the whole path, summed over `samples` straight pieces.
    float Length(int samples = 128) const {
        if (waypoints.size() < 2 || samples < 1) return 0.0f;
        float len = 0.0f;
        Vec3 prev = EvaluatePosition(0.0f);
        for (int k = 1; k <= samples; ++k) {
            Vec3 p = EvaluatePosition((float)k / samples);
            len += (p - prev).Magnitude();
            prev = p;
        }
        return len;
    }

    /// Metres along the path to the normalized position t (t is wrapped or clamped first).
    float DistanceAt(float t, int samples = 128) const {
        if (waypoints.size() < 2 || samples < 1) return 0.0f;
        t = Normalize(t);
        float len = 0.0f;
        Vec3 prev = EvaluatePosition(0.0f);
        for (int k = 1; k <= samples; ++k) {
            float tk = (float)k / samples;
            if (tk >= t) { len += (EvaluatePosition(t) - prev).Magnitude(); break; }
            Vec3 p = EvaluatePosition(tk);
            len += (p - prev).Magnitude();
            prev = p;
        }
        return len;
    }

    /// The normalized position `metres` along the path (wrapped round a loop, clamped to the ends otherwise).
    float PositionAtDistance(float metres, int samples = 128) const {
        if (waypoints.size() < 2 || samples < 1) return 0.0f;
        const float total = Length(samples);
        if (total <= 1e-6f) return 0.0f;
        if (looped) { metres = std::fmod(metres, total); if (metres < 0.0f) metres += total; }
        else if (metres <= 0.0f) return 0.0f;
        else if (metres >= total) return 1.0f;
        float len = 0.0f;
        Vec3 prev = EvaluatePosition(0.0f);
        for (int k = 1; k <= samples; ++k) {
            Vec3 p = EvaluatePosition((float)k / samples);
            float piece = (p - prev).Magnitude();
            if (len + piece >= metres) {
                float u = piece > 1e-7f ? (metres - len) / piece : 0.0f;
                return ((float)(k - 1) + u) / samples;
            }
            len += piece;
            prev = p;
        }
        return looped ? 0.0f : 1.0f;
    }

private:
    Vec3 ToWorld(const Vec3& local) const {
        return transform ? transform->TransformPoint(local) : local;
    }
    /// Clamped/wrapped waypoint access for the spline's control points.
    Vec3 P(int k) const {
        int n = (int)waypoints.size();
        if (looped) { k = ((k % n) + n) % n; return waypoints[k]; }
        return waypoints[(int)Mathf::Clamp((float)k, 0.0f, (float)(n - 1))];
    }
    /// Uniform Catmull-Rom through p1..p2: Math/Curves' CatmullRomUniform (the formula this rail always used, same bits).
    static Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float u) {
        return Curves::CatmullRomUniform(p0, p1, p2, p3, u);
    }
};

/// Drives any GameObject along a DollyPath at a speed — Cinemachine's Dolly Cart.
/// Put it on a camera (for an on-rails shot), a moving platform, or anything that
/// should travel a fixed track.
class DollyCart : public Behaviour {
public:
    std::string path;        ///< name of the GameObject holding the DollyPath
    float position = 0.0f;   ///< normalized position along the path (0..1)
    float speed = 0.1f;      ///< path fractions per second (when autoMove), or metres per second with speedInMetres
    bool  autoMove = true;   ///< advance automatically each frame
    /// Speed is metres per second along the rail, so the cart keeps one pace where the waypoints bunch up.
    bool  speedInMetres = false;
    /// On an open path, turn round at each end instead of stopping there (a looped path always goes round).
    bool  pingPong = false;
    /// Turn the object to face the way it travels (its -Z, the way a camera looks, along the rail).
    bool  orientToPath = false;

    /// +1 going forward, -1 coming back after a ping-pong turn.
    float Direction() const { return m_dir; }

    void Update(float dt) override {
        Scene* s = GetScene();
        if (!s || !transform) return;
        GameObject* pg = path.empty() ? nullptr : s->Find(path);
        DollyPath* dp = pg ? pg->GetComponent<DollyPath>() : nullptr;
        if (!dp) return;
        if (!std::isfinite(position)) position = 0.0f;
        if (autoMove && std::isfinite(dt) && std::isfinite(speed)) Advance(*dp, speed * m_dir * dt);
        // The stored position stays on the path: wrapped round a loop, clamped to an open path's ends (it used to
        // grow without limit, so a reversed cart sat still while the number came back down to 1).
        position = dp->Normalize(position);
        transform->SetPosition(dp->EvaluatePosition(position));
        if (orientToPath) {
            Vec3 tan = dp->EvaluateTangent(position);
            const float travel = (autoMove && speed != 0.0f) ? (speed * m_dir < 0.0f ? -1.0f : 1.0f) : 1.0f;
            if (tan.SqrMagnitude() > 0.5f && std::fabs(tan.y) < 0.999f) transform->SetRotation(Quat::LookRotation(tan * -travel));
        }
    }

private:
    float m_dir = 1.0f;

    void Advance(const DollyPath& dp, float step) {
        // Work in metres along the rail (speedInMetres) or in path fractions; `span` is the length of the domain.
        float span = 1.0f, at = position;
        if (speedInMetres) {
            span = dp.Length();
            if (span <= 1e-6f) return;
            at = dp.DistanceAt(position);
        }
        float next = at + step;
        if (pingPong && !dp.looped) {
            // Fold the overshoot back from the end it crossed and turn round (twice for a step past both ends).
            for (int i = 0; i < 4 && (next > span || next < 0.0f); ++i) {
                if (next > span) next = 2.0f * span - next; else next = -next;
                m_dir = -m_dir;
            }
            next = Mathf::Clamp(next, 0.0f, span);
        }
        if (speedInMetres) {
            // Clamp an open path's ends here; a loop wraps inside PositionAtDistance.
            if (!dp.looped) next = Mathf::Clamp(next, 0.0f, span);
            next = dp.PositionAtDistance(next);
        }
        position = next;
    }
};

} // namespace okay
