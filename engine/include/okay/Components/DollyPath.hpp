#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
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
    static Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float u) {
        float u2 = u * u, u3 = u2 * u;
        return 0.5f * ((2.0f * p1)
                       + u  * (p2 - p0)
                       + u2 * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3)
                       + u3 * (3.0f * p1 - 3.0f * p2 + p3 - p0));
    }
};

/// Drives any GameObject along a DollyPath at a speed — Cinemachine's Dolly Cart.
/// Put it on a camera (for an on-rails shot), a moving platform, or anything that
/// should travel a fixed track.
class DollyCart : public Behaviour {
public:
    std::string path;        ///< name of the GameObject holding the DollyPath
    float position = 0.0f;   ///< normalized position along the path (0..1)
    float speed = 0.1f;      ///< path fractions per second (when autoMove)
    bool  autoMove = true;   ///< advance automatically each frame

    void Update(float dt) override {
        Scene* s = GetScene();
        if (!s || !transform) return;
        GameObject* pg = path.empty() ? nullptr : s->Find(path);
        DollyPath* dp = pg ? pg->GetComponent<DollyPath>() : nullptr;
        if (!dp) return;
        if (autoMove) position += speed * dt;
        transform->SetPosition(dp->EvaluatePosition(position));
    }
};

} // namespace okay
