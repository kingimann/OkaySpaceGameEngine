// The particle simulation. Written from general techniques: trapezoid integration of constant acceleration,
// exponential drag (exp(-k dt), exact at any frame rate), lattice value noise, Rodrigues rotation, uniform
// sampling of a cone / ball / triangle mesh by area. The pool is a structure of arrays: kChannels floats per slot
// in one flat block, live particles packed at the front (a death moves the last live one into the hole).
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace okay {

namespace {
constexpr float kDeg2Rad = 0.0174532925f;
constexpr int   kMaxEventsPerTick = 512;
constexpr float kMaxTick = 0.05f;      // longer frames are cut into steps of at most this
constexpr float kMaxFrame = 1.0f;      // and a frame is never simulated for longer than this
inline float VLen(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
}

// What one tick shares between all particles (computed once).
struct ParticleSystem::Ctx {
    float dt = 0.0f, drag = 1.0f, windK = 0.0f;
    Vec3 origin{0, 0, 0};
    Quat rot = Quat::Identity;
    Vec3 axis{0, 1, 0};
    float orbitC = 1.0f, orbitS = 0.0f;
    bool swirl = false, vortex = false, windy = false, local = false;
    bool speedCurve = false, gravCurve = false, forceCurve = false, rotCurve = false, velCurve = false;
    bool hasLin = false, hasRadial = false, hasForce = false;
    int trailN = 0;
    void SetDt(const ParticleSystem& ps, float d) {
        dt = d;
        drag = ps.damping > 0.0f ? std::exp(-ps.damping * d) : 1.0f;
        windK = 1.0f - std::exp(-1.5f * d);
        const float orbitRad = ps.orbitSpeed * kDeg2Rad * d;
        orbitC = std::cos(orbitRad); orbitS = std::sin(orbitRad);
    }
};

// ---------------------------------------------------------------------------------------------------------
// Names and the one-line hints for the presets are in ParticlePresets.cpp.

float ParticleSystem::Hash3(int x, int y, int z) {
    std::uint32_t h = (std::uint32_t)x * 374761393u + (std::uint32_t)y * 668265263u + (std::uint32_t)z * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffffu) * (2.0f / 65535.0f) - 1.0f;
}

float ParticleSystem::Noise3(float x, float y, float z) {
    const float fx0 = std::floor(x), fy0 = std::floor(y), fz0 = std::floor(z);
    const int xi = (int)fx0, yi = (int)fy0, zi = (int)fz0;
    auto sm = [](float t) { return t * t * (3.0f - 2.0f * t); };
    const float fx = sm(x - fx0), fy = sm(y - fy0), fz = sm(z - fz0);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float c00 = lerp(Hash3(xi, yi, zi),         Hash3(xi + 1, yi, zi), fx);
    const float c10 = lerp(Hash3(xi, yi + 1, zi),     Hash3(xi + 1, yi + 1, zi), fx);
    const float c01 = lerp(Hash3(xi, yi, zi + 1),     Hash3(xi + 1, yi, zi + 1), fx);
    const float c11 = lerp(Hash3(xi, yi + 1, zi + 1), Hash3(xi + 1, yi + 1, zi + 1), fx);
    return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
}

float ParticleSystem::Hash01(std::uint64_t k) const {
    std::uint64_t z = k + seed * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (float)(z >> 40) * (1.0f / 16777216.0f);
}

Vec3 ParticleSystem::RandUnitVec() {
    const float z = m_rng.Range(-1.0f, 1.0f);
    const float a = m_rng.Range(0.0f, 6.2831853f);
    const float r = std::sqrt(std::fmax(0.0f, 1.0f - z * z));
    return Vec3{r * std::cos(a), r * std::sin(a), z};
}

// The one place the wind is applied to a particle (docs/wind.md). A wind field that varies in space plugs in here.
Vec3 ParticleSystem::WindAt(const Vec3& /*position*/, const Vec3& /*emitterOrigin*/) const {
    return WindVector() * windInfluence;
}

float ParticleSystem::LodFactor() const {
    if (lodFar <= 0.0f || lodFar <= lodNear) return 1.0f;
    if (m_viewDistance <= lodNear) return 1.0f;
    const float mn = Mathf::Clamp01(lodMinimum);
    if (m_viewDistance >= lodFar) return mn;
    const float t = (m_viewDistance - lodNear) / (lodFar - lodNear);
    return 1.0f + (mn - 1.0f) * t;
}

// ---- pool ------------------------------------------------------------------------------------------------

void ParticleSystem::EnsurePool() {
    const int cap = SafeMax(), tr = SafeTrail();
    if (cap == m_cap && tr == m_trailCap && !m_f.empty()) return;
    if (cap == 0) { m_f.clear(); m_f.shrink_to_fit(); m_trail.clear(); m_trailN.clear(); m_hit.clear(); m_cap = 0; m_trailCap = tr; m_n = 0; return; }
    std::vector<float> nf((std::size_t)kChannels * cap, 0.0f);
    const int keep = std::min(m_n, cap);
    for (int c = 0; c < kChannels; ++c)
        if (!m_f.empty() && keep > 0)
            std::memcpy(nf.data() + (std::size_t)c * cap, m_f.data() + (std::size_t)c * m_cap, sizeof(float) * keep);
    m_f.swap(nf);
    m_trail.assign((std::size_t)cap * tr * 3, 0.0f);
    m_trailN.assign((std::size_t)cap, 0);
    m_hit.assign((std::size_t)cap, 0);
    m_cap = cap; m_trailCap = tr; m_n = keep;
}

void ParticleSystem::Awake() {
    m_rng.Seed(seed);
    m_cap = 0; m_n = 0;
    EnsurePool();
    m_accum = 0.0f; m_distAccum = 0.0f; m_age = 0.0f; m_burstFired = false; m_extraFired = 0;
    m_hasLast = false; m_emitCount = 0; m_time = 0.0f;
    m_playOnReuse = playing; m_awake = true;
    if (prewarm && playing) Simulate(startLifetime + startLifetimeRandom);
}

void ParticleSystem::Restart() {
    EnsurePool();
    m_n = 0; m_age = 0.0f; m_burstFired = false; m_extraFired = 0; m_accum = 0.0f; m_distAccum = 0.0f;
    if (prewarm && playing && m_awake) Simulate(startLifetime + startLifetimeRandom);
}

void ParticleSystem::Simulate(float seconds, float step) {
    if (!(seconds > 0.0f) || !(step > 1e-4f)) return;
    if (seconds > 60.0f) seconds = 60.0f;
    float left = seconds;
    while (left > 1e-6f) { const float h = std::min(step, left); Tick(h); left -= h; }
}

int ParticleSystem::AllocSlot() {
    if (m_cap <= 0) return -1;
    if (m_n < m_cap) return m_n++;
    if (capPolicy == CapPolicy::DropNew) return -1;
    // ReplaceOldest: the oldest of 8 candidates picked by a hash (no scan of the whole pool).
    int best = -1; float bestFrac = 2.0f;
    for (int k = 0; k < 8; ++k) {
        const int j = (int)(Hash01(m_emitCount * 8u + (std::uint64_t)k + 77u) * (float)m_n) % m_n;
        const float frac = F(LIFE, j) / F(MAXLIFE, j);
        if (frac < bestFrac) { bestFrac = frac; best = j; }
    }
    return best;
}

void ParticleSystem::Remove(int i) {
    const int last = m_n - 1;
    if (i != last) {
        for (int c = 0; c < kChannels; ++c) F((Ch)c, i) = F((Ch)c, last);
        if (!m_hit.empty()) m_hit[i] = m_hit[last];
        if (m_trailCap > 0) {
            std::memcpy(&m_trail[(std::size_t)i * m_trailCap * 3], &m_trail[(std::size_t)last * m_trailCap * 3], sizeof(float) * m_trailCap * 3);
            m_trailN[i] = m_trailN[last];
        }
    }
    --m_n;
}

ParticleSystem::Particle ParticleSystem::Get(int i) const {
    Particle p;
    if (i < 0 || i >= m_n) return p;
    p.position = {F(PX, i), F(PY, i), F(PZ, i)};
    p.velocity = {F(VX, i), F(VY, i), F(VZ, i)};
    p.life = F(LIFE, i); p.maxLife = F(MAXLIFE, i);
    p.size = F(SIZE, i); p.size0 = F(SIZE0, i);
    p.rotation = F(ROT, i); p.angularVel = F(ANG, i);
    p.color = {F(R, i), F(G, i), F(B, i), F(A, i)};
    p.color0 = {F(R0, i), F(G0, i), F(B0, i), F(A0, i)};
    p.random = F(RND, i);
    p.alive = true;
    return p;
}

void ParticleSystem::Set(int i, const Particle& p) {
    if (i < 0 || i >= m_n) return;
    F(PX, i) = p.position.x; F(PY, i) = p.position.y; F(PZ, i) = p.position.z;
    F(VX, i) = p.velocity.x; F(VY, i) = p.velocity.y; F(VZ, i) = p.velocity.z;
    F(LIFE, i) = p.life; F(MAXLIFE, i) = p.maxLife;
    F(SIZE, i) = p.size; F(SIZE0, i) = p.size0;
    F(ROT, i) = p.rotation; F(ANG, i) = p.angularVel;
    F(R, i) = p.color.r; F(G, i) = p.color.g; F(B, i) = p.color.b; F(A, i) = p.color.a;
    F(R0, i) = p.color0.r; F(G0, i) = p.color0.g; F(B0, i) = p.color0.b; F(A0, i) = p.color0.a;
    F(RND, i) = p.random;
}

std::vector<ParticleSystem::Particle> ParticleSystem::Particles() const {
    std::vector<Particle> v; v.reserve((std::size_t)m_n);
    for (int i = 0; i < m_n; ++i) v.push_back(Get(i));
    return v;
}

int ParticleSystem::TrailPoints(int i, Vec3* out) const {
    if (i < 0 || i >= m_n || m_trailCap <= 0) return 0;
    const int n = m_trailN[i];
    const float* t = &m_trail[(std::size_t)i * m_trailCap * 3];
    for (int k = 0; k < n; ++k) out[k] = Vec3{t[k * 3], t[k * 3 + 1], t[k * 3 + 2]};
    return n;
}

// ---- mesh surface sampling ----------------------------------------------------------------------------------

void ParticleSystem::SampleMesh(Vec3& pos, Vec3& normal) {
    pos = Vec3::Zero; normal = Vec3{0, 1, 0};
    auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
    if (!mr) return;
    const Mesh& m = mr->DisplayMesh();
    const std::size_t tris = m.triangles.size() / 3;
    if (tris == 0 || m.vertices.empty()) return;
    if (m_meshKey != (const void*)m.vertices.data() || m_meshTris != tris || m_meshAreas.size() != tris) {
        m_meshAreas.assign(tris, 0.0f);
        float sum = 0.0f;
        for (std::size_t t = 0; t < tris; ++t) {
            const int a = m.triangles[t * 3], b = m.triangles[t * 3 + 1], c = m.triangles[t * 3 + 2];
            const int nv = (int)m.vertices.size();
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) { m_meshAreas[t] = sum; continue; }
            sum += 0.5f * VLen(Vec3::Cross(m.vertices[b] - m.vertices[a], m.vertices[c] - m.vertices[a]));
            m_meshAreas[t] = sum;   // cumulative
        }
        m_meshKey = m.vertices.data(); m_meshTris = tris;
    }
    const float total = m_meshAreas.back();
    if (total <= 1e-12f) return;
    const float r = m_rng.Range(0.0f, total);
    std::size_t t = (std::size_t)(std::lower_bound(m_meshAreas.begin(), m_meshAreas.end(), r) - m_meshAreas.begin());
    if (t >= tris) t = tris - 1;
    const int nv = (int)m.vertices.size();
    const int a = m.triangles[t * 3], b = m.triangles[t * 3 + 1], c = m.triangles[t * 3 + 2];
    if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) return;
    float u = m_rng.Range(0.0f, 1.0f), v = m_rng.Range(0.0f, 1.0f);
    if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }   // fold into the triangle: uniform by area
    const Vec3 &A = m.vertices[a], &B = m.vertices[b], &C = m.vertices[c];
    const Vec3 local = A + (B - A) * u + (C - A) * v;
    Vec3 n = Vec3::Cross(B - A, C - A);
    const float nl = VLen(n);
    n = nl > 1e-12f ? n * (1.0f / nl) : Vec3{0, 1, 0};
    if (transform) { pos = transform->TransformPoint(local); normal = transform->TransformVector(n); const float l = VLen(normal); normal = l > 1e-12f ? normal * (1.0f / l) : n; }
    else { pos = local; normal = n; }
}

// ---- spawning -------------------------------------------------------------------------------------------------

void ParticleSystem::SpawnOne(const Vec3& origin, const Vec3& extraVel, float age, float /*dt*/, const Ctx& c, bool event) {
    const int idx = AllocSlot();
    if (idx < 0) return;
    ++m_emitCount;
    float life = startLifetime + (startLifetimeRandom > 0.0f ? m_rng.Range(-startLifetimeRandom, startLifetimeRandom) : 0.0f);
    if (life < 0.01f) life = 0.01f;
    float size = startSize + (startSizeRandom > 0.0f ? m_rng.Range(-startSizeRandom, startSizeRandom) : 0.0f);
    if (size < 0.0f) size = 0.0f;
    const float rot = startRotation + (startRotationRandom > 0.0f ? m_rng.Range(-startRotationRandom, startRotationRandom) : 0.0f);
    const float ang = rotationSpeed + (rotationSpeedRandom > 0.0f ? m_rng.Range(-rotationSpeedRandom, rotationSpeedRandom) : 0.0f);
    Color col = startColor;
    if (randomStartColor) {
        const float t = m_rng.Range(0.0f, 1.0f);
        if (randomHue) {   // any colour: a random hue at the start colour's brightness and alpha
            const float h6 = t * 6.0f; const int hi = (int)h6; const float f = h6 - (float)hi;
            const float v = std::max(startColor.r, std::max(startColor.g, startColor.b));
            const float sat = 0.85f, q = v * (1.0f - sat * f), u = v * (1.0f - sat * (1.0f - f)), lo = v * (1.0f - sat);
            switch (hi % 6) {
                case 0: col = {v, u, lo, startColor.a}; break; case 1: col = {q, v, lo, startColor.a}; break;
                case 2: col = {lo, v, u, startColor.a}; break; case 3: col = {lo, q, v, startColor.a}; break;
                case 4: col = {u, lo, v, startColor.a}; break; default: col = {v, lo, q, startColor.a}; break;
            }
        } else col = Color::Lerp(startColor, startColor2, t);
    }

    Vec3 off{0, 0, 0};
    Vec3 vel = startVelocity;
    const float spd = VLen(startVelocity);
    switch (shape) {
        case Shape::Point: break;
        case Shape::Circle: {
            const float a = m_rng.Range(0.0f, 6.2831853f);
            const Vec3 dir{std::cos(a), 0.0f, std::sin(a)};
            off = dir * shapeRadius;
            vel = dir * spd + Vec3{0.0f, startVelocity.y, 0.0f};
        } break;
        case Shape::Sphere:
        case Shape::Hemisphere: {
            Vec3 dir = RandUnitVec();
            if (shape == Shape::Hemisphere && dir.y < 0.0f) dir.y = -dir.y;
            const float r = shapeSurfaceOnly ? shapeRadius : shapeRadius * std::cbrt(m_rng.Range(0.0f, 1.0f));
            off = dir * r;
            vel = dir * spd;
        } break;
        case Shape::Box: {
            off = Vec3{m_rng.Range(-boxSize.x * 0.5f, boxSize.x * 0.5f),
                       m_rng.Range(-boxSize.y * 0.5f, boxSize.y * 0.5f),
                       m_rng.Range(-boxSize.z * 0.5f, boxSize.z * 0.5f)};
            if (shapeSurfaceOnly) {   // push one coordinate out to a face, picked by face area
                const float ax = boxSize.y * boxSize.z, ay = boxSize.x * boxSize.z, az = boxSize.x * boxSize.y;
                const float pick = m_rng.Range(0.0f, ax + ay + az + 1e-12f);
                const float sign = m_rng.Range(0.0f, 1.0f) < 0.5f ? -0.5f : 0.5f;
                if (pick < ax) off.x = boxSize.x * sign; else if (pick < ax + ay) off.y = boxSize.y * sign; else off.z = boxSize.z * sign;
            }
        } break;
        case Shape::Cone: {
            const Vec3 axis = spd > 1e-5f ? startVelocity * (1.0f / spd) : Vec3{0, 1, 0};
            const float half = shapeAngle * kDeg2Rad;
            const float cosT = std::cos(half);
            const float ct = m_rng.Range(cosT, 1.0f);
            const float st = std::sqrt(std::fmax(0.0f, 1.0f - ct * ct));
            const float phi = m_rng.Range(0.0f, 6.2831853f);
            const Vec3 up = std::fabs(axis.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            Vec3 t1 = Vec3::Cross(up, axis); { const float l = VLen(t1); t1 = l > 1e-5f ? t1 * (1.0f / l) : Vec3{1, 0, 0}; }
            const Vec3 t2 = Vec3::Cross(axis, t1);
            const Vec3 dir = axis * ct + (t1 * std::cos(phi) + t2 * std::sin(phi)) * st;
            vel = dir * (spd > 1e-5f ? spd : 1.0f);
        } break;
        case Shape::Edge: {
            off = Vec3{m_rng.Range(-shapeRadius, shapeRadius), 0.0f, 0.0f};
        } break;
        case Shape::MeshSurface: {
            Vec3 p, n; SampleMesh(p, n);
            // `origin` already has the object's position in it; the sampled point is a world point: replace it.
            off = p - (transform ? transform->Position() : Vec3::Zero);
            vel = n * (spd > 1e-5f ? spd : 1.0f);
        } break;
    }
    if (speedRandom > 0.0f) vel *= (1.0f + m_rng.Range(-speedRandom, speedRandom));
    if (velocityRandom > 0.0f)
        vel += Vec3{m_rng.Range(-velocityRandom, velocityRandom), m_rng.Range(-velocityRandom, velocityRandom), m_rng.Range(-velocityRandom, velocityRandom)};
    if (inheritVelocity != 0.0f) vel += (m_emitterVel + extraVel) * inheritVelocity;

    const Vec3 pos = origin + off;
    F(PX, idx) = pos.x; F(PY, idx) = pos.y; F(PZ, idx) = pos.z;
    F(OX, idx) = pos.x; F(OY, idx) = pos.y; F(OZ, idx) = pos.z;
    F(VX, idx) = vel.x; F(VY, idx) = vel.y; F(VZ, idx) = vel.z;
    F(LIFE, idx) = life; F(MAXLIFE, idx) = life;
    F(SIZE0, idx) = size; F(SIZE, idx) = size;
    F(ROT, idx) = rot; F(ANG, idx) = ang;
    F(R0, idx) = col.r; F(G0, idx) = col.g; F(B0, idx) = col.b; F(A0, idx) = col.a;
    F(R, idx) = col.r; F(G, idx) = col.g; F(B, idx) = col.b; F(A, idx) = col.a;
    F(RND, idx) = Hash01(m_emitCount);
    F(TRT, idx) = 0.0f;
    m_hit[idx] = 0;
    if (m_trailCap > 0) m_trailN[idx] = 0;
    if (event && !subEmitterBirth.empty() && (int)m_events.size() < kMaxEventsPerTick) m_events.push_back({0, pos, Vec3{vel.x, vel.y, vel.z}});

    if (age > 1e-6f) {   // it was born part-way through the frame: bring it up to the end of the frame
        Ctx c2 = c; c2.SetDt(*this, age);
        StepRange(idx, idx + 1, age, c2);
        if (F(LIFE, idx) <= 0.0f) Remove(idx);
    }
}

void ParticleSystem::EmitAt(const Vec3& where, int count, const Vec3& extraVelocity) {
    if (count <= 0) return;
    EnsurePool();
    Ctx c; c.SetDt(*this, 0.0f);
    for (int i = 0; i < count; ++i) SpawnOne(where, extraVelocity, 0.0f, 0.0f, c, true);
}

// ---- the per-particle step (pure per index: safe to run in chunks on the job system) -----------------------

void ParticleSystem::StepRange(int begin, int end, float dt, const Ctx& c) {
    float* const px = &m_f[(std::size_t)PX * m_cap]; float* const py = &m_f[(std::size_t)PY * m_cap]; float* const pz = &m_f[(std::size_t)PZ * m_cap];
    float* const vx = &m_f[(std::size_t)VX * m_cap]; float* const vy = &m_f[(std::size_t)VY * m_cap]; float* const vz = &m_f[(std::size_t)VZ * m_cap];
    const Vec3 grav = gravity * gravityModifier;
    for (int i = begin; i < end; ++i) {
        const float life = F(LIFE, i) - dt;
        F(LIFE, i) = life;
        if (life <= 0.0f) continue;
        const float maxLife = F(MAXLIFE, i);
        const float t = 1.0f - Mathf::Clamp01(life / maxLife);
        Vec3 pos{px[i], py[i], pz[i]};
        Vec3 vel{vx[i], vy[i], vz[i]};
        Vec3 acc = c.gravCurve ? grav * gravityCurve.Evaluate(t) : grav;
        if (c.hasForce) acc += c.forceCurve ? force * forceCurve.Evaluate(t) : force;
        Vec3 dv{0, 0, 0};   // velocity changes from the wind and the vortex (small, applied half-and-half)
        if (c.windy) {
            const Vec3 w = WindAt(pos, c.origin);
            dv.x += (w.x - vel.x) * c.windK;
            dv.z += (w.z - vel.z) * c.windK;
            dv.y += w.y * dt;
        }
        if (c.vortex) {
            const Vec3 d = pos - c.origin;
            const Vec3 radial = d - c.axis * Vec3::Dot(d, c.axis);
            const float rl = VLen(radial);
            if (rl > 1e-5f) {
                const Vec3 rn = radial * (1.0f / rl);
                dv += (Vec3::Cross(c.axis, rn) * vortexStrength - rn * vortexPull) * dt;
            }
        }
        // Constant acceleration with drag has an exact answer: v' = a/k + (v - a/k) e^(-k dt), so the motion is the
        // same at 30, 60 or 144 frames per second. Without drag it is the plain trapezoid (also exact).
        Vec3 vNew, disp;
        if (c.drag != 1.0f) {
            const float k = damping;
            const Vec3 vt = acc * (1.0f / k);
            vNew = vt + (vel - vt) * c.drag;
            disp = vt * dt + (vel - vt) * ((1.0f - c.drag) / k);
        } else {
            vNew = vel + acc * dt;
            disp = (vel + vNew) * (0.5f * dt);
        }
        vNew += dv; disp += dv * (0.5f * dt);
        const float speedMul = c.speedCurve ? speedCurve.Evaluate(t) : 1.0f;
        vel = vNew;
        pos += disp * speedMul;
        if (c.hasLin || c.hasRadial) {
            const float vm = c.velCurve ? velocityCurve.Evaluate(t) : 1.0f;
            if (c.hasLin) pos += linearVelocity * (vm * dt);
            if (c.hasRadial) {
                const Vec3 d = pos - c.origin; const float l = VLen(d);
                if (l > 1e-5f) pos += d * (radialVelocity * vm * dt / l);
            }
        }
        if (noiseStrength != 0.0f) {
            Vec3 n{0, 0, 0};
            const int oct = noiseOctaves < 1 ? 1 : (noiseOctaves > 3 ? 3 : noiseOctaves);
            float amp = 1.0f, freq = noiseFrequency, norm = 0.0f;
            for (int o = 0; o < oct; ++o) {
                const Vec3 q = pos * freq;
                const float s = m_time * noiseScroll;
                n += Vec3{Noise3(q.x + s, q.y, q.z), Noise3(q.x + 31.4f, q.y + s, q.z + 17.9f), Noise3(q.x - 23.1f, q.y + 47.2f, q.z + s)} * amp;
                norm += amp; amp *= 0.5f; freq *= 2.0f;
            }
            if (oct > 1) n = n * (1.0f / norm);
            pos += n * (noiseStrength * dt);
        }
        if (orbitSpeed != 0.0f) {
            Vec3 d = pos - c.origin;
            d = d * c.orbitC + Vec3::Cross(c.axis, d) * c.orbitS + c.axis * (Vec3::Dot(c.axis, d) * (1.0f - c.orbitC));
            pos = c.origin + d;
        }
        F(ROT, i) += F(ANG, i) * (c.rotCurve ? rotationCurve.Evaluate(t) : 1.0f) * dt;
        if (collision && pos.y < collisionY && vel.y < 0.0f) {
            pos.y = collisionY;
            vel.y = -vel.y * bounce;
            const float keep = 1.0f - Mathf::Clamp01(collisionFriction);
            vel.x *= keep; vel.z *= keep;
            if (collisionLifeLoss > 0.0f) F(LIFE, i) -= maxLife * Mathf::Clamp01(collisionLifeLoss);
            if (dieOnCollision) F(LIFE, i) = 0.0f;
            m_hit[i] = 1;
        }
        px[i] = pos.x; py[i] = pos.y; pz[i] = pos.z;
        vx[i] = vel.x; vy[i] = vel.y; vz[i] = vel.z;

        if (sizeOverLife) F(SIZE, i) = sizeCurve.Active() ? F(SIZE0, i) * sizeCurve.Evaluate(t) : F(SIZE0, i) + (endSize - F(SIZE0, i)) * t;
        float cr, cg, cb, ca;
        if (colorOverLife && colorGradient.Active()) {
            const Color g = colorGradient.Evaluate(t); cr = g.r; cg = g.g; cb = g.b; ca = g.a;
        } else if (colorOverLife) {
            cr = startColor.r + (endColor.r - startColor.r) * t; cg = startColor.g + (endColor.g - startColor.g) * t;
            cb = startColor.b + (endColor.b - startColor.b) * t; ca = startColor.a + (endColor.a - startColor.a) * t;
        } else { cr = F(R0, i); cg = F(G0, i); cb = F(B0, i); ca = F(A0, i); }
        if (fadeOverLife) ca *= Mathf::Clamp01(F(LIFE, i) / maxLife);
        F(R, i) = cr; F(G, i) = cg; F(B, i) = cb; F(A, i) = ca;

        if (c.trailN > 0) {
            float& tm = F(TRT, i);
            tm += dt;
            if (tm >= trailInterval) {
                tm = std::fmod(tm, std::fmax(trailInterval, 1e-4f));
                float* tr = &m_trail[(std::size_t)i * m_trailCap * 3];
                const int n = m_trailN[i];
                const int keepN = std::min(n, c.trailN - 1);
                if (keepN > 0) std::memmove(tr + 3, tr, sizeof(float) * 3 * keepN);
                tr[0] = pos.x; tr[1] = pos.y; tr[2] = pos.z;
                m_trailN[i] = (unsigned char)(keepN + 1);
            }
        }
    }
}

// ---- world collision (serial, budgeted) -------------------------------------------------------------------------

void ParticleSystem::WorldCollide(float /*dt*/) {
    m_lastRays = 0;
    if (!worldCollision || m_n == 0 || !gameObject || !gameObject->scene()) return;
    Scene& scene = *gameObject->scene();
    Physics3D& phys = scene.physics3D();
    const int budget = collisionRaysPerFrame < 1 ? 1 : collisionRaysPerFrame;
    const int n = m_n;
    const int start = m_rayCursor % n;
    int used = 0, k = 0;
    for (; k < n && used < budget; ++k) {
        const int i = (start + k) % n;
        if (F(LIFE, i) <= 0.0f) continue;
        const Vec3 o{F(OX, i), F(OY, i), F(OZ, i)};
        const Vec3 p{F(PX, i), F(PY, i), F(PZ, i)};
        const Vec3 d = p - o;
        const float len = VLen(d);
        if (len < 1e-4f) continue;   // has not moved: nothing to hit, and the start point stays
        ++used;
        const RaycastHit3D h = phys.Raycast(scene, o, d * (1.0f / len), len, gameObject, Physics3D::kAllLayers, false);
        if (h.hit) {
            Vec3 nrm = h.normal; const float nl = VLen(nrm); nrm = nl > 1e-6f ? nrm * (1.0f / nl) : Vec3{0, 1, 0};
            const Vec3 np = h.point + nrm * 0.005f;
            F(PX, i) = np.x; F(PY, i) = np.y; F(PZ, i) = np.z;
            Vec3 v{F(VX, i), F(VY, i), F(VZ, i)};
            const float vn = Vec3::Dot(v, nrm);
            if (vn < 0.0f) {
                const Vec3 vnorm = nrm * vn;
                const Vec3 vtan = v - vnorm;
                v = vtan * (1.0f - Mathf::Clamp01(collisionFriction)) - vnorm * bounce;
            }
            F(VX, i) = v.x; F(VY, i) = v.y; F(VZ, i) = v.z;
            if (collisionLifeLoss > 0.0f) F(LIFE, i) -= F(MAXLIFE, i) * Mathf::Clamp01(collisionLifeLoss);
            if (dieOnCollision) F(LIFE, i) = 0.0f;
            m_hit[i] = 1;
            F(OX, i) = np.x; F(OY, i) = np.y; F(OZ, i) = np.z;
        } else {
            F(OX, i) = p.x; F(OY, i) = p.y; F(OZ, i) = p.z;
        }
    }
    m_rayCursor = (start + k) % std::max(1, n);
    m_lastRays = used;
}

// ---- sub-emitters ------------------------------------------------------------------------------------------------

ParticleSystem* ParticleSystem::FindSub(const std::string& name) const {
    if (name.empty() || !transform) return nullptr;
    for (Transform* t : transform->Children()) {
        if (!t || !t->gameObject || t->gameObject->name != name) continue;
        ParticleSystem* ps = t->gameObject->GetComponent<ParticleSystem>();
        if (ps && ps != this) return ps;
    }
    return nullptr;
}

void ParticleSystem::FlushEvents() {
    if (m_events.empty()) return;
    ParticleSystem* sub[3] = {FindSub(subEmitterBirth), FindSub(subEmitterDeath), FindSub(subEmitterCollision)};
    const int counts[3] = {subBirthCount, subDeathCount, subCollisionCount};
    for (const Event& e : m_events) {
        if (e.kind < 0 || e.kind > 2 || !sub[e.kind]) continue;
        sub[e.kind]->EmitAt(e.pos, counts[e.kind], e.vel);
    }
    m_events.clear();
}

// ---- one tick -------------------------------------------------------------------------------------------------------

void ParticleSystem::Update(float dt) {
    if (!(dt >= 0.0f)) return;   // negative or NaN
    if (dt > kMaxFrame) dt = kMaxFrame;
    if (!simulateOffscreen && m_culled) return;
    const int steps = dt > kMaxTick ? std::min(20, (int)std::ceil(dt / kMaxTick)) : 1;
    const float h = dt / (float)steps;
    if (steps == 1) { Tick(h); return; }
    // A long frame is cut into steps; the emitter moves a little in each, so its speed (inherited velocity) and its path are kept.
    const Vec3 end = transform ? transform->Position() : Vec3::Zero;
    const Quat rot = transform ? transform->Rotation() : Quat::Identity;
    const Vec3 start = m_hasLast ? m_lastOrigin : end;
    for (int s = 0; s < steps; ++s) {
        const Vec3 o = start + (end - start) * ((float)(s + 1) / (float)steps);
        Tick(h, &o, &rot);
    }
}

void ParticleSystem::Tick(float dt, const Vec3* originOverride, const Quat* rotOverride) {
    EnsurePool();
    if (m_cap == 0) { m_time += dt; return; }
    if (m_depth > 0) return;
    ++m_depth;

    const Vec3 origin = originOverride ? *originOverride : (transform ? transform->Position() : Vec3::Zero);
    const Quat rot = rotOverride ? *rotOverride : (transform ? transform->Rotation() : Quat::Identity);
    m_time += dt;
    const Vec3 prevOrigin = m_hasLast ? m_lastOrigin : origin;
    if (m_hasLast) {
        m_emitterVel = dt > 1e-6f ? (origin - m_lastOrigin) * (1.0f / dt) : Vec3::Zero;
        if (simulationSpace == SimulationSpace::Local) {
            const Quat dq = rot * m_lastRot.Inverse();
            for (int i = 0; i < m_n; ++i) {
                const Vec3 p = origin + dq * (Vec3{F(PX, i), F(PY, i), F(PZ, i)} - m_lastOrigin);
                const Vec3 v = dq * Vec3{F(VX, i), F(VY, i), F(VZ, i)};
                F(PX, i) = p.x; F(PY, i) = p.y; F(PZ, i) = p.z; F(VX, i) = v.x; F(VY, i) = v.y; F(VZ, i) = v.z;
            }
        }
    } else {
        m_emitterVel = Vec3::Zero;
    }
    m_lastOrigin = origin; m_lastRot = rot; m_hasLast = true;

    Ctx c;
    c.SetDt(*this, dt);
    c.origin = origin; c.rot = rot;
    c.local = simulationSpace == SimulationSpace::Local;
    c.vortex = vortexStrength != 0.0f || vortexPull != 0.0f;
    c.swirl = orbitSpeed != 0.0f || c.vortex;
    if (c.swirl) {
        const float al = VLen(orbitAxis);
        if (al > 1e-5f) c.axis = orbitAxis * (1.0f / al);
        if (c.local) c.axis = rot * c.axis;
    }
    c.speedCurve = speedOverLife && speedCurve.Active();
    c.gravCurve = gravityCurve.Active();
    c.hasForce = force.x != 0.0f || force.y != 0.0f || force.z != 0.0f;
    c.forceCurve = c.hasForce && forceCurve.Active();
    c.rotCurve = rotationOverLife && rotationCurve.Active();
    c.hasLin = linearVelocity.x != 0.0f || linearVelocity.y != 0.0f || linearVelocity.z != 0.0f;
    c.hasRadial = radialVelocity != 0.0f;
    c.velCurve = velocityCurve.Active();
    // Only while there is wind to follow (a calm scene must not slow particles down). A wind that varies in space plugs in at WindAt.
    c.windy = windInfluence != 0.0f && WindVector().SqrMagnitude() > 1e-6f;
    c.trailN = SafeTrail();

    // 1. Move what is already alive.
    const int n0 = m_n;
    if (n0 > 0) {
        std::fill(m_hit.begin(), m_hit.begin() + n0, (unsigned char)0);
        if (parallelThreshold > 0 && n0 >= parallelThreshold) {
            JobSystem::Global().ParallelFor(0, (std::size_t)n0, 1024, [&](std::size_t b, std::size_t e) { StepRange((int)b, (int)e, dt, c); });
        } else {
            StepRange(0, n0, dt, c);
        }
        WorldCollide(dt);
    } else m_lastRays = 0;

    // 2. Remove the dead, note deaths and hits, measure the box.
    Vec3 bmin{0, 0, 0}, bmax{0, 0, 0};
    const bool wantDeath = !subEmitterDeath.empty(), wantHit = !subEmitterCollision.empty();
    for (int i = 0; i < m_n;) {
        const Vec3 p{F(PX, i), F(PY, i), F(PZ, i)};
        const bool dead = F(LIFE, i) <= 0.0f;
        if (m_hit[i] && wantHit && (int)m_events.size() < kMaxEventsPerTick) m_events.push_back({2, p, Vec3{F(VX, i), F(VY, i), F(VZ, i)}});
        if (dead) {
            if (wantDeath && (int)m_events.size() < kMaxEventsPerTick) m_events.push_back({1, p, Vec3{F(VX, i), F(VY, i), F(VZ, i)}});
            Remove(i);
            continue;
        }
        ++i;
    }

    // 3. Emit: the cycle clock, bursts, the steady rate and distance emission. New particles are born part-way
    //    through the frame (on the emitter's path) and moved up to the end of it, so the spawns are smooth at any
    //    frame rate.
    bool emitting = playing;
    if (playing) {
        m_age += dt;
        if (duration > 0.0f && m_age >= duration) {
            if (loop) { m_age = std::fmod(m_age, duration); m_burstFired = false; m_extraFired = 0; }
            else      { emitting = false; }
        }
        if (emitting && burstCount > 0 && !m_burstFired && m_age >= burstTime) {
            for (int k = 0; k < burstCount; ++k) SpawnOne(origin, Vec3::Zero, 0.0f, dt, c, true);
            m_burstFired = true;
        }
        if (emitting) {
            const int nb = extraBurstCount < 0 ? 0 : (extraBurstCount > kMaxBursts ? kMaxBursts : extraBurstCount);
            for (int b = 0; b < nb; ++b) {
                if ((m_extraFired >> b) & 1u) continue;
                if (m_age >= extraBursts[b].time && extraBursts[b].count > 0) {
                    for (int k = 0; k < extraBursts[b].count; ++k) SpawnOne(origin, Vec3::Zero, 0.0f, dt, c, true);
                    m_extraFired |= 1u << b;
                }
            }
        }
    }
    if (emitting && dt > 0.0f) {
        const float rate = emissionRate * LodFactor();
        if (rate > 0.0f) {
            const float before = m_accum;
            m_accum += rate * dt;
            const int cnt = (int)m_accum;
            m_accum -= (float)cnt;
            for (int j = 0; j < cnt; ++j) {
                float tj = ((float)(j + 1) - before) / rate;   // seconds into this frame
                tj = std::min(std::max(tj, 0.0f), dt);
                const float f = tj / dt;
                SpawnOne(prevOrigin + (origin - prevOrigin) * f, Vec3::Zero, dt - tj, dt, c, true);
            }
        }
        if (emitPerDistance > 0.0f) {
            const float dist = VLen(origin - prevOrigin);
            const float want = emitPerDistance * dist;
            if (want > 0.0f) {
                const float before = m_distAccum;
                m_distAccum += want;
                const int cnt = (int)m_distAccum;
                m_distAccum -= (float)cnt;
                for (int j = 0; j < cnt; ++j) {
                    const float f = std::min(1.0f, ((float)(j + 1) - before) / want);
                    SpawnOne(prevOrigin + (origin - prevOrigin) * f, Vec3::Zero, dt * (1.0f - f), dt, c, true);
                }
            }
        }
    }

    // 4. A box around everything alive (the renderers cull with it).
    if (m_n > 0) {
        bmin = bmax = Vec3{F(PX, 0), F(PY, 0), F(PZ, 0)};
        for (int i = 1; i < m_n; ++i) {
            const float x = F(PX, i), y = F(PY, i), z = F(PZ, i);
            bmin.x = std::min(bmin.x, x); bmin.y = std::min(bmin.y, y); bmin.z = std::min(bmin.z, z);
            bmax.x = std::max(bmax.x, x); bmax.y = std::max(bmax.y, y); bmax.z = std::max(bmax.z, z);
        }
    } else { bmin = bmax = origin; }
    m_bmin = bmin; m_bmax = bmax;

    --m_depth;
    FlushEvents();
}

} // namespace okay
