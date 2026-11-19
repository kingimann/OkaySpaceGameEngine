#pragma once
// Buoyancy: makes a body with a Rigidbody float, bob, tilt, slow down in the water and splash.
//
// Written from the plain physics of floating (Archimedes: the upward push equals the weight of the water the hull
// pushes aside) and of drag, in our own words. Nothing here is copied from another engine.
//
// How it works, every physics step:
//   1. The hull is a handful of sample points (a grid across the collider, or your own Buoyancy Points list).
//      Each point stands for an equal share of the hull's volume.
//   2. At each point we ask the Water for the wave height and slope there (Water::SurfaceAt). The part of the point's
//      share that is under the surface pushes up with  density * gravity * volume * submergedFraction  at that
//      point, so a hull that sits deeper on one side is pushed back level (it tilts and rights itself).
//   3. Drag is applied as a velocity change relative to the moving water, separately for the hull's forward, sideways
//      and up directions (linear + quadratic terms). It is done as an exact "slow down by this factor" step, so a big
//      time step can never make a body speed up: energy only goes down.
//   4. Angular damping calms spin the same way.
//   5. A point that dives into the water fast (or leaves it fast) raises a splash event.
// Runs in FixedUpdate, so it behaves the same at 30, 60 or 144 frames per second.
//
// Setup: a Rigidbody3D + a collider on the object, and a Water in the scene. A Water with Depth 0 counts as a flat
// sheet with no bottom, so things float on it too. Water volumes that already float plain props (Water.linearDrag)
// leave an object with this component alone.
#include "okay/Components/Water.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/Vec3.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace okay {

namespace waterfx {
/// A small world-space spray effect (white drops thrown up that fall back and fade). Made on first use.
inline ParticleSystem* MakeSpray(Scene* scene, const char* name, float lifetime = 0.8f) {
    if (!scene) return nullptr;
    GameObject* go = scene->CreateGameObject(name);
    auto* ps = go->AddComponent<ParticleSystem>();
    ps->emissionRate = 0.0f;
    ps->maxParticles = 160;
    ps->playing = true;
    ps->loop = true;
    ps->startLifetime = lifetime;
    ps->startLifetimeRandom = lifetime * 0.3f;
    ps->startSize = 0.22f;
    ps->startSizeRandom = 0.08f;
    ps->sizeOverLife = true;
    ps->endSize = 0.04f;
    ps->startColor = Color{0.92f, 0.97f, 1.0f, 0.85f};
    ps->startVelocity = {0.0f, 2.6f, 0.0f};
    ps->velocityRandom = 1.1f;
    ps->gravity = {0.0f, -9.0f, 0.0f};
    ps->gravityModifier = 1.0f;
    ps->windInfluence = 0.0f;
    ps->simulationSpace = ParticleSystem::SimulationSpace::World;
    ps->Awake();   // size the particle pool now; the scene would only do it next frame
    return ps;
}
} // namespace waterfx

class Buoyancy : public Behaviour {
public:
    /// One splash: where, how hard (the speed the point hit or left the water at, m/s), and which way.
    struct Splash { Vec3 position; float strength = 0.0f; bool entering = true; };

    // ---- Hull ----
    /// Hull sample points in the object's own space (before its scale and turn). Empty = make a grid from the collider.
    std::vector<Vec3> points;
    int   gridX = 3;            ///< automatic points across the width
    int   gridZ = 3;            ///< automatic points along the length
    /// Volume of water the whole hull pushes aside when fully under, in cubic metres. 0 = take it from the collider's
    /// bounding box times `fullness`.
    float volume = 0.0f;
    /// How much of the bounding box the hull really fills: 1 = a solid block, 0.5 = a rowing boat shell.
    float fullness = 1.0f;
    /// Height of the part of the hull that goes under water as it sinks (metres). 0 = the collider's height.
    float hullHeight = 0.0f;

    // ---- Water ----
    float density = 1.0f;       ///< 0 = use the Water's own density. Otherwise mass per cubic metre of the water.
    /// Drag that slows the body relative to the water, per second (linear) and per metre/second (quadratic),
    /// for the three hull directions: along the nose, sideways, and up/down.
    float dragForward = 0.3f,  dragForward2 = 0.05f;
    float dragSide    = 1.0f,  dragSide2    = 0.3f;
    float dragUp      = 2.0f,  dragUp2      = 0.3f;
    float angularDamping = 1.5f;   ///< spin slows by this per second while under water
    float maxPushAcceleration = 120.0f;   ///< the push is never more than this many m/s^2 (keeps tiny bodies stable)
    bool  letTilt = true;       ///< switch the Rigidbody's "freeze rotation" off at Start so the hull can lean on waves

    // ---- Splashes ----
    bool  splashEffect = true;           ///< throw white spray where a point hits the water
    float splashMinSpeed = 1.2f;         ///< slower than this is no splash (m/s)
    std::function<void(const Splash&)> onSplash;   ///< also called for each splash (not saved)

    // ---- Read-outs (not saved) ----
    float Submerged() const { return m_submerged; }   ///< 0..1 how much of the hull is under water right now
    bool  InWater() const { return m_submerged > 0.01f; }
    int   SplashCount() const { return m_splashCount; }
    const Splash& LastSplash() const { return m_lastSplash; }
    /// Surface under the hull centre as last seen.
    const Water::Surface& SurfaceUnder() const { return m_surface; }
    float Draft() const { return m_draft; }           ///< metres of the hull below the waterline at its centre

    bool IgnoresBuoyancy() const override { return true; }   // the plain-prop buoyancy of Water leaves this body alone

    void Start() override {
        auto* rb = Body();
        if (rb && letTilt) rb->freezeRotation = false;
    }

    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        if (m_spray && dying.count(m_spray->gameObject)) m_spray = nullptr;
    }
    void OnDestroy() override {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (sc && m_spray && m_spray->gameObject) sc->Destroy(m_spray->gameObject);
        m_spray = nullptr;
    }

    /// The hull points in world space (what the force is applied at) and the volume and height one point stands for.
    /// Public so tests, the editor's gizmo and a boat's wake can see them.
    void WorldPoints(std::vector<Vec3>& out, float& cellVolume, float& cellHeight) const {
        out.clear();
        Vec3 c, h; Quat q;
        const bool haveBounds = Bounds(c, h, q);
        const float boxVolume = 8.0f * h.x * h.y * h.z;
        const float totalVolume = volume > 0.0f ? volume : std::max(0.0f, boxVolume * fullness);
        cellHeight = hullHeight > 0.0f ? hullHeight : (haveBounds ? std::max(0.05f, 2.0f * h.y) : 1.0f);
        if (!points.empty()) {
            for (const Vec3& p : points) out.push_back(transform ? transform->TransformPoint(p) : p);
        } else {
            const int nx = std::max(1, std::min(gridX, 16)), nz = std::max(1, std::min(gridZ, 16));
            for (int iz = 0; iz < nz; ++iz)
                for (int ix = 0; ix < nx; ++ix) {
                    // cell centres, evenly spread across the footprint
                    const float fx = nx == 1 ? 0.0f : ((ix + 0.5f) / nx * 2.0f - 1.0f);
                    const float fz = nz == 1 ? 0.0f : ((iz + 0.5f) / nz * 2.0f - 1.0f);
                    out.push_back(c + q * Vec3{fx * h.x, 0.0f, fz * h.z});
                }
        }
        cellVolume = out.empty() ? 0.0f : totalVolume / (float)out.size();
    }

    /// The points as text "x,y,z;x,y,z" (how they are saved and edited as one property).
    std::string PointsText() const {
        std::string t;
        char b[96];
        for (const Vec3& p : points) {
            std::snprintf(b, sizeof b, "%g,%g,%g", p.x, p.y, p.z);
            if (!t.empty()) t += ';';
            t += b;
        }
        return t;
    }
    /// Read points from that text. Bad pieces are skipped; at most 64 points are kept.
    void SetPointsText(const std::string& t) {
        points.clear();
        size_t i = 0;
        while (i < t.size() && points.size() < 64) {
            size_t j = t.find(';', i);
            if (j == std::string::npos) j = t.size();
            float x, y, z;
            if (std::sscanf(t.substr(i, j - i).c_str(), "%f , %f , %f", &x, &y, &z) == 3 &&
                std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) points.push_back({x, y, z});
            i = j + 1;
        }
    }

    /// Centre, half sizes (world units) and turn of the hull box the points are laid out in. False when there is no
    /// collider (then it is a one-metre block scaled by the object).
    bool HullBounds(Vec3& centre, Vec3& half, Quat& q) const { return Bounds(centre, half, q); }

    void FixedUpdate(float dt) override {
        Rigidbody3D* rb = Body();
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!rb || !sc || !transform || dt <= 0.0f) return;
        if (rb->bodyType != Rigidbody3D::BodyType::Dynamic || rb->mass <= 1e-4f) return;
        if (!std::isfinite(rb->velocity.x + rb->velocity.y + rb->velocity.z)) return;   // never feed a broken body more

        const std::vector<Water*> waters = sc->FindObjectsOfType<Water>();
        std::vector<Vec3> pts; float cellVol = 0.0f, cellH = 1.0f;
        WorldPoints(pts, cellVol, cellH);
        if (pts.empty()) return;
        if (m_wasIn.size() != pts.size()) { m_wasIn.assign(pts.size(), 0); m_cool.assign(pts.size(), 0.0f); }

        constexpr float kG = 9.81f;
        float sumFrac = 0.0f, pushTotal = 0.0f;
        Vec3 flowSum{0, 0, 0}; int flowN = 0;
        float rho = density;
        Water::Surface sur;
        struct Hit { Vec3 p; float frac; float vy; };
        std::vector<Hit> hits;
        hits.reserve(pts.size());
        Vec3 push{0, 0, 0}, torque{0, 0, 0};
        const Vec3 com = transform->Position();
        for (size_t i = 0; i < pts.size(); ++i) {
            m_cool[i] = std::max(0.0f, m_cool[i] - dt);
            float frac = 0.0f;
            const Vec3 p = pts[i];
            if (Water::SampleIn(waters, p, sur, 0.0f) && p.y > sur.floorY) {
                if (density <= 0.0f) rho = FluidDensityAt(waters, p);
                const float bottom = p.y - cellH * 0.5f;
                frac = std::min(1.0f, std::max(0.0f, (sur.height - bottom) / cellH));
                flowSum = flowSum + sur.velocity; ++flowN;
                if (frac > 0.0f) {
                    const float f = rho * kG * cellVol * frac;
                    push.y += f;
                    pushTotal += f;
                    torque = torque + Vec3::Cross(p - com, Vec3{0.0f, f, 0.0f});
                }
                // splash detection: relative speed of this point to the water, vertical part
                const Vec3 vp = PointVelocity(rb, p, com);
                const float rel = vp.y - sur.velocity.y;
                const bool inNow = frac > 0.02f;
                if (inNow && !m_wasIn[i] && rel < -splashMinSpeed && m_cool[i] <= 0.0f)
                    RaiseSplash({Vec3{p.x, sur.height, p.z}, -rel, true}, i);
                else if (!inNow && m_wasIn[i] && rel > splashMinSpeed * 1.5f && m_cool[i] <= 0.0f)
                    RaiseSplash({Vec3{p.x, sur.height, p.z}, rel, false}, i);
                m_wasIn[i] = inNow ? 1 : 0;
                m_surface = sur;
            } else {
                m_wasIn[i] = 0;
            }
            sumFrac += frac;
        }
        m_submerged = sumFrac / (float)pts.size();

        // ---- push (clamped so a very light body cannot be flung) ----
        const float maxPush = maxPushAcceleration * rb->mass;
        if (pushTotal > maxPush && pushTotal > 0.0f) { const float k = maxPush / pushTotal; push = push * k; torque = torque * k; }
        if (pushTotal > 0.0f) {
            rb->AddForce(push);
            if (!rb->freezeRotation) rb->AddTorque(torque);
        }

        // ---- draft at the centre (for read-outs and wakes) ----
        {
            Water::Surface c;
            if (Water::SampleIn(waters, com, c, 0.0f)) {
                m_draft = std::max(0.0f, c.height - (com.y - cellH * 0.5f));
                m_surface = c;
            } else m_draft = 0.0f;
        }

        // ---- drag against the moving water, by hull direction ----
        if (m_submerged > 1e-4f) {
            const Vec3 flow = flowN > 0 ? flowSum * (1.0f / (float)flowN) : Vec3{0, 0, 0};
            const Vec3 fw = transform->Forward(), rt = transform->Right(), up = transform->Up();
            const Vec3 rel = rb->velocity - flow;
            const float vf = Vec3::Dot(rel, fw), vs = Vec3::Dot(rel, rt), vu = Vec3::Dot(rel, up);
            const float s = m_submerged;
            auto slow = [&](float v, float lin, float quad) {
                const float k = 1.0f + (std::max(0.0f, lin) + std::max(0.0f, quad) * std::fabs(v)) * s * dt;
                return v / k;
            };
            const Vec3 nv = flow + fw * slow(vf, dragForward, dragForward2) + rt * slow(vs, dragSide, dragSide2)
                          + up * slow(vu, dragUp, dragUp2);
            rb->velocity = nv;
            if (!rb->freezeRotation && angularDamping > 0.0f)
                rb->angularVelocity = rb->angularVelocity * (1.0f / (1.0f + angularDamping * s * dt));
        }
    }

private:
    Rigidbody3D* Body() const { return gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr; }

    static Vec3 PointVelocity(const Rigidbody3D* rb, const Vec3& p, const Vec3& com) {
        return rb->velocity + Vec3::Cross(rb->angularVelocity, p - com);
    }
    static float FluidDensityAt(const std::vector<Water*>& waters, const Vec3& p) {
        Water::Surface s; float best = -1.0e9f, d = 1.0f;
        for (Water* w : waters) {
            if (!w || !w->enabled || !w->gameObject || !w->gameObject->active) continue;
            if (w->SurfaceAt(p, s, 0.0f) && s.height > best) { best = s.height; d = w->fluidDensity; }
        }
        return d > 0.0f ? d : 1.0f;
    }

    // Local-space box of the hull (centre and half sizes in world units, plus the object's turn).
    bool Bounds(Vec3& centre, Vec3& half, Quat& q) const {
        q = transform ? transform->Rotation() : Quat::Identity;
        centre = transform ? transform->Position() : Vec3::Zero;
        half = Vec3{0.5f, 0.5f, 0.5f};
        if (!gameObject) return false;
        for (Collider3D* col : gameObject->GetComponents<Collider3D>()) {
            if (!col || col->isTrigger) continue;
            if (auto* box = dynamic_cast<BoxCollider3D*>(col)) {
                half = box->HalfExtents();
                centre = box->WorldCenter();
                return true;
            }
            Vec3 mn, mx; col->WorldAABB(mn, mx);   // sphere / capsule: upright, so the bounds are the shape
            centre = Vec3{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f};
            half = Vec3{(mx.x - mn.x) * 0.5f, (mx.y - mn.y) * 0.5f, (mx.z - mn.z) * 0.5f};
            return true;
        }
        if (transform) {   // no collider: a one-metre block scaled by the object
            const Vec3 s = transform->LossyScale();
            half = Vec3{std::fabs(s.x) * 0.5f, std::fabs(s.y) * 0.5f, std::fabs(s.z) * 0.5f};
        }
        return false;
    }

    void RaiseSplash(const Splash& s, size_t i) {
        m_lastSplash = s;
        ++m_splashCount;
        m_cool[i] = 0.2f;
        if (onSplash) onSplash(s);
        if (!splashEffect) return;
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc) return;
        if (!m_spray) m_spray = waterfx::MakeSpray(sc, "Splash");
        if (!m_spray || !m_spray->transform) return;
        m_spray->transform->SetPosition(s.position);
        m_spray->startVelocity = {0.0f, std::min(6.0f, 1.2f + s.strength * 0.7f), 0.0f};
        m_spray->Emit(std::min(24, 4 + (int)(s.strength * 3.0f)));
    }

    float m_submerged = 0.0f, m_draft = 0.0f;
    int m_splashCount = 0;
    Splash m_lastSplash;
    Water::Surface m_surface;
    std::vector<char> m_wasIn;
    std::vector<float> m_cool;
    ParticleSystem* m_spray = nullptr;
};

} // namespace okay
