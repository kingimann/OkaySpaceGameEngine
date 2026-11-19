// Wind Zone, Wind Drag, Wind Sway, Wind Ambience. See docs/wind.md.
#include "okay/Components/WindZone.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace okay {

namespace {
std::vector<WindZone*>& Registry() { static std::vector<WindZone*> r; return r; }
bool Finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}

// ---------------------------------------------------------------- WindZone

const char* WindZone::ModeName(Mode m) {
    switch (m) {
        case Mode::Everywhere: return "Everywhere";
        case Mode::Direction:  return "Direction";
        case Mode::Outward:    return "Pushes outward";
    }
    return "Direction";
}
const char* WindZone::ModeHint(Mode m) {
    switch (m) {
        case Mode::Everywhere: return "The wind of the whole scene. Turn the object to aim it (the arrow shows the way).";
        case Mode::Direction:  return "Wind inside a box, ball or tube, blowing the way the arrow points. Turn the object to aim it.";
        case Mode::Outward:    return "Wind that blows away from the middle (a fan, a blast). A negative strength sucks in.";
    }
    return "";
}

Vec3 WindZone::WorldDirection() const {
    if (!transform || !Finite3(direction) || direction.SqrMagnitude() < 1e-12f) return Vec3{0, 0, 0};
    const Vec3 w = transform->TransformDirection(direction.Normalized());
    return Finite3(w) ? w.Normalized() : Vec3{0, 0, 0};
}

std::vector<std::pair<float, float>> WindZone::ParseCurve(const std::string& text) {
    std::vector<std::pair<float, float>> out;
    std::string s = text;
    for (char& c : s) if (c == ',' || c == ';') c = ' ';
    std::istringstream in(s);
    std::string tok;
    while (in >> tok) {
        const std::size_t colon = tok.find(':');
        if (colon == std::string::npos) continue;
        char* e1 = nullptr; char* e2 = nullptr;
        const std::string a = tok.substr(0, colon), b = tok.substr(colon + 1);
        const float t = std::strtof(a.c_str(), &e1), v = std::strtof(b.c_str(), &e2);
        if (a.empty() || b.empty() || *e1 != '\0' || *e2 != '\0') continue;
        if (!std::isfinite(t) || !std::isfinite(v) || t < 0.0f) continue;
        out.emplace_back(t, std::max(0.0f, v));
        if (out.size() >= 64) break;
    }
    return out;
}
std::string WindZone::FormatCurve(const std::vector<std::pair<float, float>>& keys) {
    std::ostringstream o;
    for (std::size_t i = 0; i < keys.size(); ++i) { if (i) o << ", "; o << keys[i].first << ":" << keys[i].second; }
    return o.str();
}

WindSource WindZone::Snapshot() const {
    WindSource s;
    s.global = mode == Mode::Everywhere;
    s.kind = mode == Mode::Outward ? WindSource::Kind::Radial : WindSource::Kind::Directional;
    s.priority = priority; s.additive = additive;
    s.strength = std::isfinite(strength) ? strength : 0.0f;
    s.direction = WorldDirection();
    s.gustAmount = std::isfinite(gustAmount) ? gustAmount : 0.0f;
    s.gustFrequency = std::isfinite(gustFrequency) ? gustFrequency : 0.0f;
    s.gustSpeed = std::isfinite(gustSpeed) ? gustSpeed : 0.0f;
    s.turbulence = std::isfinite(turbulence) ? turbulence : 0.0f;
    s.turbulenceScale = std::isfinite(turbulenceScale) ? turbulenceScale : 0.15f;
    s.seed = (std::uint32_t)seed;
    s.edge = std::isfinite(edge) ? std::clamp(edge, 0.0f, 1.0f) : 0.0f;
    s.falloff = std::isfinite(falloff) ? std::clamp(falloff, 0.0f, 1.0f) : 0.0f;
    s.shape = (WindSource::Shape)std::clamp(shape, 0, 2);
    if (!curveKeys.empty()) { s.curve.keys = ParseCurve(curveKeys); s.curve.length = std::isfinite(curveLength) ? curveLength : 20.0f; }
    if (!s.global) {
        if (!transform) { s.half = {0, 0, 0}; return s; }
        const Vec3 sc = transform->LossyScale();
        s.center = transform->TransformPoint(offset);
        s.axisX = transform->Right(); s.axisY = transform->Up(); s.axisZ = transform->Forward();
        auto h = [](float size, float scale) { const float v = size * 0.5f * std::fabs(scale); return std::isfinite(v) && v > 0.0f ? v : 0.0f; };
        s.half = {h(size.x, sc.x), h(size.y, sc.y), h(size.z, sc.z)};
    }
    return s;
}

Vec3 WindZone::WindAt(const Vec3& world) const {
    const WindSource s = Snapshot();
    const float c = s.Coverage(world);
    if (!(c > 0.0f)) return Vec3{0, 0, 0};
    return s.Raw(world, WindField::Clock()) * c;
}

WindZone* WindZone::FindGlobal(Scene& scene) {
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || go->IsDestroyed()) continue;
        for (WindZone* z : go->GetComponents<WindZone>()) if (z && z->mode == Mode::Everywhere) return z;
    }
    return nullptr;
}
WindZone* WindZone::EnsureGlobal(Scene& scene) {
    if (WindZone* z = FindGlobal(scene)) return z;
    GameObject* go = scene.CreateGameObject("Wind");
    auto* z = go->AddComponent<WindZone>();
    z->mode = Mode::Everywhere; z->strength = 0.0f; z->additive = true;
    return z;
}
void WindZone::Aim(const Vec3& d) {
    if (!transform || !Finite3(d) || d.SqrMagnitude() < 1e-10f) return;
    direction = {0, 0, 1};
    transform->SetRotation(Quat::LookRotation(d.Normalized()));
}

void WindZone::OnEnable() {
    auto& r = Registry();
    if (std::find(r.begin(), r.end(), this) == r.end()) r.push_back(this);
}
void WindZone::OnDisable() {
    auto& r = Registry();
    r.erase(std::remove(r.begin(), r.end(), this), r.end());
}
WindZone::~WindZone() { OnDisable(); }

const std::vector<WindZone*>& WindZone::Live() { return Registry(); }

void WindZone::UpdateAll(float dt) {
    if (std::isfinite(dt) && dt > 0.0f) {
        float& t = WindField::Clock();
        t += dt;
        if (t > 1.0e6f) t = std::fmod(t, 1.0e6f);   // keep the float precise in a very long session
    }
    const auto& r = Registry();
    if (r.empty()) { if (WindField::Active()) WindField::Publish({}); return; }
    std::vector<WindSource> src;
    src.reserve(r.size());
    for (WindZone* z : r) {
        if (!z || !z->enabled || !z->gameObject || !z->gameObject->active) continue;
        src.push_back(z->Snapshot());
    }
    WindField::Publish(std::move(src));
}

// ---------------------------------------------------------------- WindDrag

Vec3 WindDrag::Force(const Vec3& rel, float area, float cd, float mass, float step) {
    if (!Finite3(rel) || !(area > 0.0f) || !(cd > 0.0f)) return Vec3{0, 0, 0};
    const float speed = rel.Magnitude();
    if (!(speed > 1e-4f)) return Vec3{0, 0, 0};
    float f = 0.5f * kAirDensity * cd * area * speed * speed;   // N, along `rel`
    if (mass > 0.0f && step > 0.0f) f = std::min(f, mass * speed / step);   // never overshoot the wind in one step
    return rel * (f / speed);
}

void WindDrag::FixedUpdate(float step) {
    if (!gameObject || !(strength > 0.0f)) return;
    std::vector<Rigidbody3D*> bodies;
    if (auto* rb = gameObject->GetComponent<Rigidbody3D>()) bodies.push_back(rb);
    if (includeChildren && transform) {
        std::vector<Transform*> stack(transform->Children().begin(), transform->Children().end());
        while (!stack.empty() && bodies.size() < 256) {
            Transform* t = stack.back(); stack.pop_back();
            if (!t || !t->gameObject) continue;
            if (auto* rb = t->gameObject->GetComponent<Rigidbody3D>()) bodies.push_back(rb);
            for (Transform* c : t->Children()) stack.push_back(c);
        }
    }
    if (bodies.empty()) return;
    const float share = area / (float)bodies.size();
    for (Rigidbody3D* rb : bodies) {
        if (!rb || rb->bodyType != Rigidbody3D::BodyType::Dynamic || !rb->transform) continue;
        const Vec3 rel = WindField::At(rb->transform->Position()) - rb->velocity;
        if (rel.SqrMagnitude() < 0.25f) continue;   // under half a metre a second: leave a sleeping body asleep
        rb->AddForce(Force(rel, share, dragCoefficient, rb->mass, step) * std::clamp(strength, 0.0f, 10.0f));
    }
}

// ---------------------------------------------------------------- WindSway

float WindSway::Bend(float w, float speed, float amount, float stiff) {
    if (!(w > 0.0f) || !std::isfinite(w) || !std::isfinite(speed) || !std::isfinite(amount)) return 0.0f;
    const float ww = std::min(1.0f, w);
    return amount * WindField::Lean(speed) * std::pow(ww, std::clamp(std::isfinite(stiff) ? stiff : 2.0f, 0.5f, 6.0f));
}

void WindSway::Capture() {
    m_hasBase = false;
    auto* mr = gameObject ? gameObject->GetComponent<MeshRenderer>() : nullptr;
    if (!mr) return;
    const auto& v = mr->mesh.vertices;
    m_base = v;
    m_w.clear();
    if (v.empty()) return;
    m_minY = m_maxY = v[0].y; m_minX = m_maxX = v[0].x;
    float minZ = v[0].z, maxZ = v[0].z;
    for (const Vec3& p : v) {
        m_minY = std::min(m_minY, p.y); m_maxY = std::max(m_maxY, p.y); m_minX = std::min(m_minX, p.x); m_maxX = std::max(m_maxX, p.x);
        minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
    }
    m_center = {(m_minX + m_maxX) * 0.5f, (m_minY + m_maxY) * 0.5f, (minZ + maxZ) * 0.5f};
    const float H = height > 0.0f ? height : std::max(0.01f, m_maxY - m_minY);
    const float W = std::max(0.01f, m_maxX - m_minX);
    m_w.resize(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        float w;
        if (weight == Weight::Custom && m_custom.size() == v.size()) w = m_custom[i];
        else if (weight == Weight::Flag) w = (v[i].x - m_minX) / W;
        else w = (v[i].y - m_minY) / H;
        m_w[i] = std::clamp(std::isfinite(w) ? w : 0.0f, 0.0f, 1.0f);
    }
    m_hasBase = true;
}

void WindSway::Restore() {
    if (!m_hasBase || !gameObject) return;
    auto* mr = gameObject->GetComponent<MeshRenderer>();
    if (mr && mr->mesh.vertices.size() == m_base.size()) mr->mesh.vertices = m_base;
}
void WindSway::OnDisable() { Restore(); m_hasBase = false; }

void WindSway::Update(float dt) {
    if (!std::isfinite(dt) || dt < 0.0f) return;
    m_time += dt; m_timer += dt;
    const float period = updateRate > 0.5f ? 1.0f / updateRate : 0.05f;
    if (m_hasBase && m_timer < period) return;
    Step(m_timer);
    m_timer = 0.0f;
}

void WindSway::Step(float dt) {
    if (!gameObject || !transform) return;
    auto* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) return;
    auto& verts = mr->mesh.vertices;
    if (verts.empty()) return;
    // The mesh was replaced by someone else (a rebuild, an edit): measure it again.
    if (!m_hasBase || m_base.size() != verts.size()) Capture();
    else if (verts.size() > 0 && (verts.front() != m_lastFront || verts.back() != m_lastBack)) Capture();
    if (!m_hasBase) return;
    const Vec3 pos = transform->TransformPoint(m_center);   // the middle of the mesh: a merged Foliage chunk is far from its object's origin
    if (maxDistance > 0.0f) {
        Scene* sc = GetScene();
        if (sc && sc->mainCamera && sc->mainCamera->transform &&
            (sc->mainCamera->transform->Position() - pos).Magnitude() > maxDistance) return;
    }
    Vec3 wind = WindField::At(pos);
    if (!Finite3(wind)) wind = Vec3{0, 0, 0};
    const float speed = wind.Magnitude();
    Vec3 hor{wind.x, 0.0f, wind.z};
    const float hlen = hor.Magnitude();
    const Vec3 dirH = hlen > 1e-4f ? hor / hlen : Vec3{0, 0, 0};
    const float lean = WindField::Lean(speed);

    if (weight == Weight::Flag && turnToWind && hlen > 0.3f) {   // point the flag downwind, smoothly
        const Vec3 r = transform->Right();
        const Vec3 rh{r.x, 0.0f, r.z};
        if (rh.SqrMagnitude() > 1e-6f) {
            const Quat delta = Quat::FromToRotation(rh.Normalized(), dirH);
            const Quat target = delta * transform->Rotation();
            const Vec3 poleLocal{m_minX, m_center.y, m_center.z};          // the edge on the pole stays where it is
            const Vec3 poleBefore = transform->TransformPoint(poleLocal);
            transform->SetRotation(Quat::Slerp(transform->Rotation(), target, std::min(1.0f, std::max(dt, 0.0f) * 2.0f)));
            transform->SetPosition(transform->Position() + (poleBefore - transform->TransformPoint(poleLocal)));
        }
    }

    const Vec3 dirL = transform->InverseTransformVector(dirH);   // one metre downwind, in the mesh's own units
    const float H = height > 0.0f ? height : std::max(0.01f, m_maxY - m_minY);
    const float t = m_time;
    const float fl = std::clamp(flutter, 0.0f, 1.0f);
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const float w = m_w[i];
        Vec3 p = m_base[i];
        if (w > 0.0f && (lean > 0.0f)) {
            if (weight == Weight::Flag) {
                // A wave that runs from the pole to the free edge; stronger wind, bigger and faster waves.
                const float amp = amount * (0.15f + lean) * w;
                const float ph = 6.2831853f * (w * 1.6f - t * flutterSpeed * (0.25f + 0.5f * lean));
                p.z += std::sin(ph) * amp;
                p.y += std::sin(ph * 1.3f + 1.0f) * amp * 0.25f - (1.0f - std::min(1.0f, lean * 2.0f)) * w * amount * 0.5f;
            } else {
                const float b = Bend(w, speed, amount, stiffness);
                p += dirL * b;
                p.y -= 0.5f * b * b / H;   // a bent stalk gets a little shorter
                if (fl > 0.0f) {
                    const float sc = 2.5f, tt = t * flutterSpeed * 0.5f;
                    const Vec3 n{windmath::Noise3(m_base[i].x * sc + tt, m_base[i].y * sc, m_base[i].z * sc, 5u),
                                 windmath::Noise3(m_base[i].x * sc, m_base[i].y * sc + tt, m_base[i].z * sc, 6u),
                                 windmath::Noise3(m_base[i].x * sc, m_base[i].y * sc, m_base[i].z * sc + tt, 7u)};
                    p += n * (fl * amount * 0.15f * std::min(1.0f, lean + 0.1f) * w);
                }
            }
        }
        verts[i] = p;
    }
    m_lastFront = verts.front(); m_lastBack = verts.back();
}

// ---------------------------------------------------------------- WindAmbience

float WindAmbience::Loudness(float speed, float quietBelow, float fullAt) {
    if (!(speed > quietBelow) || !std::isfinite(speed)) return 0.0f;
    if (!(fullAt > quietBelow)) return 1.0f;
    const float t = std::clamp((speed - quietBelow) / (fullAt - quietBelow), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

AudioClip WindAmbience::MakeClip(float seconds, std::uint32_t seed, int rate) {
    AudioClip c;
    c.sampleRate = rate > 1000 ? rate : 22050;
    const int n = std::max(1024, (int)(std::clamp(seconds, 0.5f, 60.0f) * (float)c.sampleRate));
    c.samples.resize((std::size_t)n);
    // Our own xorshift for white noise; a two-stage low-pass (the "whoosh") whose cutoff breathes slowly.
    std::uint32_t s = seed ? seed : 1u;
    auto white = [&s]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(s >> 8) * (2.0f / 16777215.0f) - 1.0f; };
    const int pre = c.sampleRate / 2;                 // run in first so the filters are settled
    const int fade = std::min(c.sampleRate / 2, n / 2);
    std::vector<float> raw((std::size_t)(pre + n + fade));
    float a = 0.0f, b = 0.0f;
    for (int i = 0; i < pre + n + fade; ++i) {
        const float tt = (float)(i - pre) / (float)c.sampleRate;
        const float breath = 0.5f + 0.5f * std::sin(6.2831853f * tt * 3.0f / std::max(0.5f, seconds) + 0.7f);   // 3 swells per loop
        const float k = 0.02f + 0.10f * breath;                                  // low-pass coefficient: duller to brighter
        a += (white() - a) * k;
        b += (a - b) * std::min(1.0f, k * 1.6f);
        raw[(std::size_t)i] = b * (4.0f + 6.0f * breath);
    }
    // Seamless loop: the sound continues past the end as raw[pre + n + i]; the first `fade` samples slide from
    // that continuation into the clip's own start, so the last sample flows straight into the first.
    for (int i = 0; i < n; ++i) c.samples[(std::size_t)i] = raw[(std::size_t)(pre + i)];
    for (int i = 0; i < fade; ++i) {
        const float f = (float)i / (float)fade;
        c.samples[(std::size_t)i] = raw[(std::size_t)(pre + i)] * std::sin(f * 1.5707963f) + raw[(std::size_t)(pre + n + i)] * std::cos(f * 1.5707963f);
    }
    float peak = 1e-6f;
    for (float v : c.samples) peak = std::max(peak, std::fabs(v));
    for (float& v : c.samples) v = std::clamp(v / peak * 0.8f, -1.0f, 1.0f);
    return c;
}

void WindAmbience::Start() {
    if (!gameObject) return;
    auto* au = gameObject->GetComponent<AudioSource>();
    if (!au) au = gameObject->AddComponent<AudioSource>();
    if (au->Clip().samples.empty() && au->clipPath.empty()) au->clip = MakeClip();
    au->loop = true; au->spatial = false; au->volume = 0.0f;
}

void WindAmbience::Update(float dt) {
    if (!gameObject || !std::isfinite(dt)) return;
    auto* au = gameObject->GetComponent<AudioSource>();
    if (!au) return;
    Vec3 at = transform ? transform->Position() : Vec3{0, 0, 0};
    if (Scene* sc = GetScene()) if (sc->mainCamera && sc->mainCamera->transform) at = sc->mainCamera->transform->Position();
    const float target = Loudness(WindField::At(at).Magnitude(), quietBelow, fullAt) * std::clamp(volume, 0.0f, 1.0f);
    const float k = smoothing > 0.01f ? std::min(1.0f, dt / smoothing) : 1.0f;
    m_level += (target - m_level) * k;
    au->volume = m_level;
    au->pitch = 0.85f + 0.4f * std::min(1.0f, m_level / std::max(0.01f, volume));
    if (m_level > 0.003f) { if (!au->IsPlaying()) au->Play(); }
    else if (au->IsPlaying()) au->Stop();
}

} // namespace okay
