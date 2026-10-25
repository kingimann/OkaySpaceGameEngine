#include "okay/Core/Tween.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UIRawImage.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

// ---------------------------------------------------------------- Tweener ----

float Tweener::FullDuration() const {
    if (loops < 0) return std::numeric_limits<float>::infinity();
    return delay + Duration() * (float)loops;
}

float Tweener::ElapsedPercentage() const {
    const float p = Elapsed(), d = Duration();
    if (m_complete) return 1.0f;
    return Mathf::Clamp01((p - std::floor(p / d) * d) / d);
}

void Tweener::Advance(float dt) {
    if (m_killed || paused) return;
    if (!backwards && m_complete) return;
    const float next = m_time + (backwards ? -dt : dt) * timeScale;
    if (backwards && next <= 0.0f) { RenderAt(0.0f); paused = true; return; }
    RenderAt(next);
}

void Tweener::RenderAt(float time) {
    if (m_killed) return;
    const float full = FullDuration();
    if (time < 0.0f) time = 0.0f;
    if (time > full) time = full;
    m_time = time;
    const float p = time - delay;
    if (p < 0.0f) {   // still in the delay (or rewound into it): show the start
        if (m_initialized) DoApply(Easing::Evaluate(ease, 0.0f), 0.0f, 0);
        m_complete = false; m_loop = 0;
        return;
    }
    if (!m_initialized) { m_initialized = true; DoInit(); }
    if (!m_started) { m_started = true; if (onStart) onStart(); }

    const float d = Duration();
    const bool finite = loops > 0;
    const bool done = finite && p >= d * (float)loops;
    int loop = done ? loops - 1 : (int)std::floor(p / d);
    float local = done ? d : p - (float)loop * d;
    const int completed = done ? loops : loop;
    // Loops finished since the last frame: play each one's end first, so a
    // sequence fires what's at the end of a loop even when a big step skips past it.
    for (int L = m_loop; L < completed && L < m_loop + 64; ++L) {
        if (L == completed - 1 && done) break;   // the final end is applied below
        const float ue = (loopType == LoopType::Yoyo && (L % 2) == 1) ? 0.0f : 1.0f;
        DoApply(Easing::Evaluate(ease, ue), ue, loopType == LoopType::Incremental ? L : 0);
        if (loopType == LoopType::Restart) DoLoopRestart();
    }

    float u = Mathf::Clamp01(local / d);
    if (loopType == LoopType::Yoyo && (loop % 2) == 1) u = 1.0f - u;
    const int inc = loopType == LoopType::Incremental ? loop : 0;
    DoApply(Easing::Evaluate(ease, u), u, inc);
    if (onUpdate) onUpdate();

    if (completed > m_loop && onStepComplete)
        for (int i = m_loop; i < completed; ++i) onStepComplete();
    m_loop = completed;
    if (done && !m_complete) { m_complete = true; if (onComplete) onComplete(); }
    else if (!done) m_complete = false;
}

void Tweener::Restart(bool includeDelay) {
    if (m_killed) return;
    m_complete = false; m_loop = 0; paused = false; backwards = false;
    DoLoopRestart();
    RenderAt(includeDelay ? 0.0f : delay);
}

void Tweener::Rewind() {
    if (m_killed) return;
    m_complete = false; m_loop = 0; backwards = false;
    DoLoopRestart();
    RenderAt(0.0f);
    paused = true;
}

void Tweener::Complete() {
    if (m_killed) return;
    if (loops < 0) {   // endless: finish the loop it's in
        const float d = Duration();
        RenderAt(delay + (std::floor(Elapsed() / d) + 1.0f) * d - 1e-5f);
        if (onComplete) onComplete();
        return;
    }
    RenderAt(FullDuration());
}

void Tweener::Kill(bool complete) {
    if (m_killed) return;
    if (complete) Complete();
    m_killed = true;
    if (onKill) onKill();
}

void Tweener::Goto(float seconds, bool andPlay) {
    if (m_killed) return;
    if (seconds < Elapsed()) { m_loop = 0; m_complete = false; DoLoopRestart(); }
    RenderAt(delay + seconds);
    paused = !andPlay;
}

void Tweener::DropDying(const std::unordered_set<const GameObject*>& dying) {
    if (target && dying.count(target)) m_killed = true;   // silent: its object is gone
}

// ------------------------------------------------------------- ValueTween ----

void ValueTween::DoInit() {
    const Values cur = m_get ? m_get() : Values{};
    Values tgt{};
    for (int i = 0; i < m_count; ++i) tgt[i] = relative ? cur[i] + m_end[i] : m_end[i];
    m_from = fromMode ? tgt : cur;
    m_to   = fromMode ? cur : tgt;
}

void ValueTween::DoApply(float k, float, int loop) {
    if (!m_set) return;
    Values v = m_from;
    for (int i = 0; i < m_count; ++i) v[i] = m_from[i] + (m_to[i] - m_from[i]) * (k + (float)loop);
    m_set(v);
}

// ---------------------------------------------------------- TweenSequence ----

float TweenSequence::ChildLength(const Tweener& t) {
    return t.delay + t.Duration() * (float)(t.loops < 0 ? 1 : t.loops);
}

void TweenSequence::Adopt(const std::shared_ptr<Tweener>& t) {
    if (t->m_owner) t->m_owner->Detach(t.get());
    t->m_owner = nullptr;
    t->m_nested = true;
    t->paused = false;
}

bool TweenSequence::Contains(const Tweener* t) const {
    if (t == this) return true;
    for (const Item& it : m_items) {
        if (it.tween.get() == t) return true;
        if (auto* s = dynamic_cast<const TweenSequence*>(it.tween.get()); s && s->Contains(t)) return true;
    }
    return false;
}

TweenSequence& TweenSequence::Insert(float at, std::shared_ptr<Tweener> t) {
    if (!t || t.get() == this) return *this;
    // A sequence that (at any depth) already holds this one would make a loop:
    // endless recursion when it plays, and a shared_ptr cycle that never frees.
    if (auto* s = dynamic_cast<const TweenSequence*>(t.get()); s && s->Contains(this)) return *this;
    Adopt(t);
    if (at < 0) at = 0;
    m_items.push_back({t, at});
    m_end = std::max(m_end, at + ChildLength(*t));
    duration = m_end;
    return *this;
}

TweenSequence& TweenSequence::Append(std::shared_ptr<Tweener> t) {
    const float at = m_end;
    Insert(at, std::move(t));
    m_lastStart = at;
    return *this;
}

TweenSequence& TweenSequence::Join(std::shared_ptr<Tweener> t) {
    return Insert(m_lastStart, std::move(t));
}

TweenSequence& TweenSequence::AppendInterval(float seconds) {
    if (seconds > 0) { m_end += seconds; m_lastStart = m_end; duration = m_end; }
    return *this;
}

TweenSequence& TweenSequence::PrependInterval(float seconds) {
    if (seconds <= 0) return *this;
    for (auto& it : m_items) it.at += seconds;
    for (auto& c : m_calls) c.at += seconds;
    m_lastStart += seconds; m_end += seconds; duration = m_end;
    return *this;
}

TweenSequence& TweenSequence::AppendCallback(Callback fn) { return InsertCallback(m_end, std::move(fn)); }

TweenSequence& TweenSequence::InsertCallback(float at, Callback fn) {
    m_calls.push_back({at < 0 ? 0 : at, std::move(fn)});
    if (at > m_end) { m_end = at; duration = m_end; }
    return *this;
}

void TweenSequence::DoApply(float k, float, int) {
    const float total = Duration();
    const float T = Mathf::Clamp(k, 0.0f, 1.0f) * total;
    const bool forward = T >= m_prevT;
    // Children in time order (reverse order when going back, so the earliest one
    // writes last and the start state wins).
    std::vector<std::size_t> order(m_items.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return forward ? m_items[a].at < m_items[b].at : m_items[a].at > m_items[b].at;
    });
    for (std::size_t i : order) {
        Tweener& c = *m_items[i].tween;
        if (c.m_killed) continue;
        const float ct = T - m_items[i].at;
        if (ct < 0.0f) { if (c.m_initialized) c.RenderAt(0.0f); continue; }   // not reached (yet / any more)
        c.RenderAt(std::min(ct, ChildLength(c)));
    }
    for (const Call& cl : m_calls)
        if (forward && cl.fn && ((cl.at > m_prevT && cl.at <= T) || (cl.at == 0.0f && m_prevT < 0.0f))) cl.fn();
    m_prevT = T;
}

void TweenSequence::DoLoopRestart() {
    for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
        Tweener& c = *it->tween;
        if (c.m_killed || !c.m_initialized) continue;
        c.m_loop = 0; c.m_complete = false;
        c.RenderAt(0.0f);
    }
    m_prevT = -1.0f;
}

void TweenSequence::DropDying(const std::unordered_set<const GameObject*>& dying) {
    Tweener::DropDying(dying);
    for (auto& it : m_items) it.tween->DropDying(dying);
}

// ----------------------------------------------------------- TweenManager ----

TweenManager::~TweenManager() = default;

void TweenManager::Adopt(const std::shared_ptr<Tweener>& t) {
    if (!t || t->m_owner == this) return;
    t->m_owner = this;
    t->m_handle = m_next++;
    m_tweens.push_back(t);
}

void TweenManager::Detach(Tweener* t) {
    for (auto it = m_tweens.begin(); it != m_tweens.end(); ++it)
        if (it->get() == t) { m_tweens.erase(it); return; }
}

std::shared_ptr<TweenSequence> TweenManager::Sequence() {
    return Add(std::make_shared<TweenSequence>());
}

std::shared_ptr<ValueTween> TweenManager::To(std::function<float()> get, std::function<void(float)> set,
                                             float end, float duration) {
    auto t = std::make_shared<ValueTween>(1,
        [get]() { return ValueTween::Values{get ? get() : 0.0f, 0, 0, 0}; },
        [set](const ValueTween::Values& v) { if (set) set(v[0]); },
        ValueTween::Values{end, 0, 0, 0});
    t->duration = duration;
    return Add(t);
}

std::shared_ptr<ValueTween> TweenManager::Virtual(float from, float to, float duration, std::function<void(float)> set) {
    auto t = std::make_shared<ValueTween>(1,
        [from]() { return ValueTween::Values{from, 0, 0, 0}; },
        [set](const ValueTween::Values& v) { if (set) set(v[0]); },
        ValueTween::Values{to, 0, 0, 0});
    t->duration = duration;
    return Add(t);
}

std::shared_ptr<Tweener> TweenManager::DelayedCall(float seconds, std::function<void()> fn) {
    auto t = std::make_shared<CustomTween>(nullptr, nullptr);
    t->duration = seconds > 0 ? seconds : 1e-4f;
    t->ease = Ease::Linear;
    t->onComplete = std::move(fn);
    return Add(std::shared_ptr<Tweener>(t));
}

void TweenManager::Update(float dt, float unscaledDt) {
    // Tweens may be added (or killed) by callbacks while we go, so walk by index.
    for (std::size_t i = 0; i < m_tweens.size(); ++i) {
        std::shared_ptr<Tweener> t = m_tweens[i];   // keep alive through its callbacks
        if (t->m_killed) continue;
        t->Advance(t->unscaledTime ? unscaledDt : dt);
        if (!t->m_killed && t->m_complete && t->autoKill && !t->backwards) t->Kill();
    }
    m_tweens.erase(std::remove_if(m_tweens.begin(), m_tweens.end(),
                                  [](const std::shared_ptr<Tweener>& t) { return t->m_killed; }),
                   m_tweens.end());
}

std::shared_ptr<Tweener> TweenManager::Find(Handle h) const {
    for (const auto& t : m_tweens) if (t->m_handle == h && !t->m_killed) return t;
    return nullptr;
}

std::vector<std::shared_ptr<Tweener>> TweenManager::WithId(const std::string& id) const {
    std::vector<std::shared_ptr<Tweener>> out;
    for (const auto& t : m_tweens) if (!t->m_killed && t->id == id) out.push_back(t);
    return out;
}

std::vector<std::shared_ptr<Tweener>> TweenManager::OfTarget(const GameObject* go) const {
    std::vector<std::shared_ptr<Tweener>> out;
    for (const auto& t : m_tweens) if (!t->m_killed && go && t->target == go) out.push_back(t);
    return out;
}

int TweenManager::Kill(const std::string& id, bool complete) {
    int n = 0; for (auto& t : WithId(id)) { t->Kill(complete); ++n; } return n;
}
int TweenManager::KillTarget(const GameObject* go, bool complete) {
    int n = 0; for (auto& t : OfTarget(go)) { t->Kill(complete); ++n; } return n;
}
void TweenManager::KillAll(bool complete) {
    std::vector<std::shared_ptr<Tweener>> all = m_tweens;
    for (auto& t : all) t->Kill(complete);
}
int TweenManager::Pause(const std::string& id)    { int n = 0; for (auto& t : WithId(id)) { t->Pause(); ++n; } return n; }
int TweenManager::Play(const std::string& id)     { int n = 0; for (auto& t : WithId(id)) { t->Play(); ++n; } return n; }
int TweenManager::Restart(const std::string& id)  { int n = 0; for (auto& t : WithId(id)) { t->Restart(); ++n; } return n; }
int TweenManager::Complete(const std::string& id) { int n = 0; for (auto& t : WithId(id)) { t->Complete(); ++n; } return n; }
int TweenManager::PauseTarget(const GameObject* go) { int n = 0; for (auto& t : OfTarget(go)) { t->Pause(); ++n; } return n; }
int TweenManager::PlayTarget(const GameObject* go)  { int n = 0; for (auto& t : OfTarget(go)) { t->Play(); ++n; } return n; }

void TweenManager::PauseAll(bool keepUnscaled) {
    for (auto& t : m_tweens) if (!(keepUnscaled && t->unscaledTime)) t->Pause();
}
void TweenManager::PlayAll() { for (auto& t : m_tweens) t->Play(); }

std::size_t TweenManager::Count() const {
    std::size_t n = 0; for (const auto& t : m_tweens) if (!t->m_killed) ++n; return n;
}
std::size_t TweenManager::PlayingCount() const {
    std::size_t n = 0; for (const auto& t : m_tweens) if (t->IsPlaying()) ++n; return n;
}
bool TweenManager::IsTweening(const GameObject* go) const {
    for (const auto& t : m_tweens) if (!t->m_killed && t->target == go && !t->paused) return true;
    return false;
}

void TweenManager::OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) {
    for (auto& t : m_tweens) t->DropDying(dying);
    m_tweens.erase(std::remove_if(m_tweens.begin(), m_tweens.end(),
                                  [](const std::shared_ptr<Tweener>& t) { return t->m_killed; }),
                   m_tweens.end());
}

void TweenManager::Clear() { m_tweens.clear(); }

// --------------------------------------------------------------- DOTween ----

namespace DOTween {
namespace {

// A tween that isn't in any manager (no object / scene): chaining on it is harmless.
std::shared_ptr<Tweener> Dummy() { return std::make_shared<CustomTween>(nullptr, nullptr); }

std::shared_ptr<Tweener> Manage(GameObject* go, std::shared_ptr<Tweener> t, float duration) {
    t->duration = duration;
    t->target = go;
    if (!go || !go->scene()) return t;
    return go->scene()->tweens().Add(t);
}

using V = ValueTween::Values;

// A tween over a Vec3 read / written by functions.
std::shared_ptr<Tweener> Vec3Tween(GameObject* go, std::function<Vec3()> get, std::function<void(const Vec3&)> set,
                                   const Vec3& to, float duration) {
    auto t = std::make_shared<ValueTween>(3,
        [get]() { Vec3 v = get(); return V{v.x, v.y, v.z, 0}; },
        [set](const V& v) { set(Vec3{v[0], v[1], v[2]}); },
        V{to.x, to.y, to.z, 0});
    return Manage(go, t, duration);
}

// Deterministic noise in -1..1 for a shake step.
float Noise(std::uint32_t seed, int step, int axis) {
    std::uint32_t h = seed * 747796405u + (std::uint32_t)step * 2891336453u + (std::uint32_t)axis * 1181783497u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return (float)(h & 0xFFFFFF) / (float)0x7FFFFF - 1.0f;
}

// Shared by the punch tweens: 0 at the ends, oscillating `vibrato` times, fading.
float PunchCurve(float u, int vibrato, float elasticity) {
    float s = std::sin(u * Mathf::PI * (float)std::max(1, vibrato));
    if (s < 0.0f) s *= Mathf::Clamp01(elasticity);
    return s * (1.0f - u);
}

// Shared by the shake tweens: random offsets every 1/vibrato, blended smoothly.
Vec3 ShakeOffset(std::uint32_t seed, float u, float duration, int vibrato, bool fadeOut, const Vec3& strength) {
    const float steps = std::max(1.0f, (float)std::max(1, vibrato) * std::max(duration, 0.05f));
    const float f = u * steps; const int i = (int)f; const float w = f - (float)i;
    const float sw = w * w * (3.0f - 2.0f * w);
    auto at = [&](int step, int axis) { return Noise(seed, step, axis); };
    Vec3 o{at(i, 0) + (at(i + 1, 0) - at(i, 0)) * sw,
           at(i, 1) + (at(i + 1, 1) - at(i, 1)) * sw,
           at(i, 2) + (at(i + 1, 2) - at(i, 2)) * sw};
    const float fade = fadeOut ? (1.0f - u) : (u < 1.0f ? 1.0f : 0.0f);
    return Vec3{o.x * strength.x, o.y * strength.y, o.z * strength.z} * fade;
}

std::uint32_t g_shakeSeed = 1;

} // namespace

std::shared_ptr<Tweener> Move(GameObject* go, const Vec3& to, float duration) {
    if (!go || !go->transform) return Dummy();
    Transform* tr = go->transform;
    return Vec3Tween(go, [tr]() { return tr->Position(); }, [tr](const Vec3& v) { tr->SetPosition(v); }, to, duration);
}

std::shared_ptr<Tweener> LocalMove(GameObject* go, const Vec3& to, float duration) {
    if (!go || !go->transform) return Dummy();
    Transform* tr = go->transform;
    return Vec3Tween(go, [tr]() { return tr->localPosition; }, [tr](const Vec3& v) { tr->localPosition = v; }, to, duration);
}

std::shared_ptr<Tweener> MoveAxis(GameObject* go, int axis, float to, float duration) {
    if (!go || !go->transform || axis < 0 || axis > 2) return Dummy();
    Transform* tr = go->transform;
    auto t = std::make_shared<ValueTween>(1,
        [tr, axis]() { const Vec3& p = tr->localPosition; return V{axis == 0 ? p.x : axis == 1 ? p.y : p.z, 0, 0, 0}; },
        [tr, axis](const V& v) { (axis == 0 ? tr->localPosition.x : axis == 1 ? tr->localPosition.y : tr->localPosition.z) = v[0]; },
        V{to, 0, 0, 0});
    return Manage(go, t, duration);
}

std::shared_ptr<Tweener> Scale(GameObject* go, const Vec3& to, float duration) {
    if (!go || !go->transform) return Dummy();
    Transform* tr = go->transform;
    return Vec3Tween(go, [tr]() { return tr->localScale; }, [tr](const Vec3& v) { tr->localScale = v; }, to, duration);
}

std::shared_ptr<Tweener> Scale(GameObject* go, float to, float duration) { return Scale(go, Vec3{to, to, to}, duration); }

std::shared_ptr<Tweener> Rotate(GameObject* go, const Vec3& eulerDeg, float duration) {
    if (!go || !go->transform) return Dummy();
    Transform* tr = go->transform;
    auto t = std::make_shared<CustomTween>(nullptr, nullptr);
    auto start = std::make_shared<Quat>();
    Tweener* self = t.get();
    t->SetFunctions([tr, start]() { *start = tr->localRotation; },
                    [tr, start, eulerDeg, self](float k, float, int loop) {
        if (self->relative) {   // spin by the angles: full turns work
            const float a = self->fromMode ? (1.0f - k) : k;
            tr->localRotation = (*start * Quat::Euler(eulerDeg * (a + (float)loop))).Normalized();
        } else {
            const Quat end = Quat::Euler(eulerDeg);
            tr->localRotation = self->fromMode ? Quat::Slerp(end, *start, k) : Quat::Slerp(*start, end, k);
        }
    });
    return Manage(go, t, duration);
}

namespace {
std::shared_ptr<Tweener> Punch(GameObject* go, int what, const Vec3& punch, float duration, int vibrato, float elasticity) {
    if (!go || !go->transform) return Dummy();
    Transform* tr = go->transform;
    auto t = std::make_shared<CustomTween>(nullptr, nullptr);
    auto start = std::make_shared<std::pair<Vec3, Quat>>();
    t->ease = Ease::Linear;
    t->SetFunctions([tr, start, what]() {
        start->first = what == 0 ? tr->localPosition : tr->localScale;
        start->second = tr->localRotation;
    }, [tr, start, what, punch, vibrato, elasticity](float, float u, int) {
        const float c = PunchCurve(u, vibrato, elasticity);
        if (what == 0) tr->localPosition = start->first + punch * c;
        else if (what == 1) tr->localScale = start->first + punch * c;
        else tr->localRotation = (start->second * Quat::Euler(punch * c)).Normalized();
    });
    return Manage(go, t, duration);
}
std::shared_ptr<Tweener> Shake(GameObject* go, int what, float duration, const Vec3& strength, int vibrato, bool fadeOut) {
    if (!go || !go->transform) return Dummy();
    Transform* tr = go->transform;
    auto t = std::make_shared<CustomTween>(nullptr, nullptr);
    auto start = std::make_shared<std::pair<Vec3, Quat>>();
    const std::uint32_t seed = g_shakeSeed++;
    t->ease = Ease::Linear;
    t->SetFunctions([tr, start, what]() {
        start->first = what == 0 ? tr->localPosition : tr->localScale;
        start->second = tr->localRotation;
    }, [tr, start, what, seed, duration, strength, vibrato, fadeOut](float, float u, int) {
        const Vec3 o = ShakeOffset(seed, u, duration, vibrato, fadeOut, strength);
        if (what == 0) tr->localPosition = start->first + o;
        else if (what == 1) tr->localScale = start->first + o;
        else tr->localRotation = (start->second * Quat::Euler(o)).Normalized();
    });
    return Manage(go, t, duration);
}
} // namespace

std::shared_ptr<Tweener> PunchPosition(GameObject* go, const Vec3& p, float d, int v, float e) { return Punch(go, 0, p, d, v, e); }
std::shared_ptr<Tweener> PunchScale(GameObject* go, const Vec3& p, float d, int v, float e)    { return Punch(go, 1, p, d, v, e); }
std::shared_ptr<Tweener> PunchRotation(GameObject* go, const Vec3& p, float d, int v, float e) { return Punch(go, 2, p, d, v, e); }
std::shared_ptr<Tweener> ShakePosition(GameObject* go, float d, const Vec3& s, int v, bool f) { return Shake(go, 0, d, s, v, f); }
std::shared_ptr<Tweener> ShakeScale(GameObject* go, float d, const Vec3& s, int v, bool f)    { return Shake(go, 1, d, s, v, f); }
std::shared_ptr<Tweener> ShakeRotation(GameObject* go, float d, const Vec3& s, int v, bool f) { return Shake(go, 2, d, s, v, f); }

std::shared_ptr<Tweener> Jump(GameObject* go, const Vec3& to, float power, int jumps, float duration) {
    if (!go || !go->transform) return Dummy();
    Transform* tr = go->transform;
    auto t = std::make_shared<CustomTween>(nullptr, nullptr);
    auto start = std::make_shared<Vec3>();
    Tweener* self = t.get();
    jumps = std::max(1, jumps);
    t->SetFunctions([tr, start]() { *start = tr->localPosition; },
                    [tr, start, to, power, jumps, self](float k, float u, int) {
        const Vec3 end = self->relative ? *start + to : to;
        Vec3 p = *start + (end - *start) * k;
        float f = u * (float)jumps; f -= std::floor(f);
        if (u >= 1.0f) f = 0.0f;
        p.y += power * 4.0f * f * (1.0f - f);
        tr->localPosition = p;
    });
    return Manage(go, t, duration);
}

std::shared_ptr<Tweener> Path(GameObject* go, std::vector<Vec3> points, float duration, PathType type, bool closed) {
    if (!go || !go->transform || points.empty()) return Dummy();
    Transform* tr = go->transform;
    auto t = std::make_shared<CustomTween>(nullptr, nullptr);
    auto pts = std::make_shared<std::vector<Vec3>>();
    t->SetFunctions([tr, pts, points, closed]() {
        pts->clear(); pts->push_back(tr->localPosition);
        pts->insert(pts->end(), points.begin(), points.end());
        if (closed) pts->push_back(pts->front());
    }, [tr, pts, type](float k, float, int) {
        const int segs = (int)pts->size() - 1;
        if (segs < 1) return;
        const float f = Mathf::Clamp01(k) * (float)segs;
        int i = std::min((int)f, segs - 1);
        const float s = f - (float)i;
        const auto& P = *pts;
        if (type == PathType::CatmullRom) {
            const Vec3& p0 = P[std::max(0, i - 1)], &p1 = P[i], &p2 = P[i + 1], &p3 = P[std::min(segs, i + 2)];
            const float s2 = s * s, s3 = s2 * s;
            tr->localPosition = (p1 * 2.0f + (p2 - p0) * s + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * s2
                                 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * s3) * 0.5f;
        } else {
            tr->localPosition = P[i] + (P[i + 1] - P[i]) * s;
        }
    });
    return Manage(go, t, duration);
}

Color* ColorOf(GameObject* go) {
    if (!go) return nullptr;
    if (auto* c = go->GetComponent<SpriteRenderer>()) return &c->color;
    if (auto* c = go->GetComponent<MeshRenderer>())   return &c->color;
    if (auto* c = go->GetComponent<UIImage>())        return &c->color;
    if (auto* c = go->GetComponent<UIRawImage>())     return &c->color;
    if (auto* c = go->GetComponent<UIPanel>())        return &c->color;
    if (auto* c = go->GetComponent<UIButton>())       return &c->color;
    if (auto* c = go->GetComponent<TextRenderer>())   return &c->color;
    return nullptr;
}

std::shared_ptr<Tweener> ColorTo(GameObject* go, const Color& to, float duration) {
    Color* c = ColorOf(go);
    if (!c) return Dummy();
    auto t = std::make_shared<ValueTween>(4,
        [c]() { return V{c->r, c->g, c->b, c->a}; },
        [c](const V& v) { c->r = v[0]; c->g = v[1]; c->b = v[2]; c->a = v[3]; },
        V{to.r, to.g, to.b, to.a});
    return Manage(go, t, duration);
}

std::shared_ptr<Tweener> Fade(GameObject* go, float alpha, float duration) {
    Color* c = ColorOf(go);
    if (!c) return Dummy();
    auto t = std::make_shared<ValueTween>(1, [c]() { return V{c->a, 0, 0, 0}; },
                                          [c](const V& v) { c->a = Mathf::Clamp01(v[0]); }, V{alpha, 0, 0, 0});
    return Manage(go, t, duration);
}

std::shared_ptr<Tweener> AnchorPos(GameObject* go, const Vec2& to, float duration) {
    if (!go || !GetUIRect(go).position) return Dummy();
    auto t = std::make_shared<ValueTween>(2,
        [go]() { UIRect r = GetUIRect(go); return r.position ? V{r.position->x, r.position->y, 0, 0} : V{}; },
        [go](const V& v) { UIRect r = GetUIRect(go); if (r.position) *r.position = Vec2{v[0], v[1]}; },
        V{to.x, to.y, 0, 0});
    return Manage(go, t, duration);
}

std::shared_ptr<Tweener> SizeDelta(GameObject* go, const Vec2& to, float duration) {
    if (!go || !GetUIRect(go).sizePtr) return Dummy();
    auto t = std::make_shared<ValueTween>(2,
        [go]() { UIRect r = GetUIRect(go); return r.sizePtr ? V{r.sizePtr->x, r.sizePtr->y, 0, 0} : V{}; },
        [go](const V& v) { UIRect r = GetUIRect(go); if (r.sizePtr) *r.sizePtr = Vec2{v[0], v[1]}; },
        V{to.x, to.y, 0, 0});
    return Manage(go, t, duration);
}

std::shared_ptr<Tweener> Text(GameObject* go, const std::string& text, float duration) {
    auto* tr = go ? go->GetComponent<TextRenderer>() : nullptr;
    if (!tr) return Dummy();
    // Character boundaries (UTF-8), so a letter is never cut in half.
    auto cuts = std::make_shared<std::vector<std::size_t>>();
    for (std::size_t i = 0; i < text.size(); ++i)
        if (((unsigned char)text[i] & 0xC0) != 0x80) cuts->push_back(i);
    cuts->push_back(text.size());
    auto t = std::make_shared<CustomTween>(nullptr, nullptr);
    t->ease = Ease::Linear;
    t->SetFunctions([tr]() { tr->text.clear(); }, [tr, text, cuts](float k, float, int) {
        const std::size_t chars = cuts->size() - 1;
        const std::size_t n = (std::size_t)std::lround(Mathf::Clamp01(k) * (float)chars);
        tr->text = text.substr(0, (*cuts)[std::min(n, chars)]);
    });
    return Manage(go, t, duration);
}

std::shared_ptr<Tweener> Counter(GameObject* go, float from, float to, float duration, const std::string& prefix) {
    auto* tr = go ? go->GetComponent<TextRenderer>() : nullptr;
    if (!tr) return Dummy();
    auto t = std::make_shared<ValueTween>(1, [from]() { return V{from, 0, 0, 0}; },
        [tr, prefix](const V& v) { tr->text = prefix + std::to_string((long long)std::lround(v[0])); }, V{to, 0, 0, 0});
    return Manage(go, t, duration);
}

namespace {
TweenManager* ManagerOf(GameObject* go) { return go && go->scene() ? &go->scene()->tweens() : nullptr; }
}
int Kill(GameObject* go, bool complete) { auto* m = ManagerOf(go); return m ? m->KillTarget(go, complete) : 0; }
int Pause(GameObject* go)    { auto* m = ManagerOf(go); return m ? m->PauseTarget(go) : 0; }
int Play(GameObject* go)     { auto* m = ManagerOf(go); return m ? m->PlayTarget(go) : 0; }
int Restart(GameObject* go)  { auto* m = ManagerOf(go); int n = 0; if (m) for (auto& t : m->OfTarget(go)) { t->Restart(); ++n; } return n; }
int Complete(GameObject* go) { auto* m = ManagerOf(go); int n = 0; if (m) for (auto& t : m->OfTarget(go)) { t->Complete(); ++n; } return n; }

} // namespace DOTween
} // namespace okay
