#include "okay/Components/CharacterAnimBackend.hpp"
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Component.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

namespace okay {

namespace {
float frac01(float v) { return v - std::floor(v); }

// Mix `p` (with root `r`) into a running weighted mix: the first sample is taken
// as is, each later one is slerped in by its share of the weight so far.
void MixIn(std::vector<Vec3>& pose, Vec3& root, float& total, const std::vector<Vec3>& p, const Vec3& r, float w) {
    if (w <= 0.0f || p.empty()) return;
    if (total <= 0.0f) { pose = p; root = r; total = w; return; }
    total += w;
    const float u = w / total;
    for (std::size_t b = 0; b < pose.size() && b < p.size(); ++b) pose[b] = AnimClip::BlendEuler(pose[b], p[b], u);
    root = root + (r - root) * u;
}

// The built-in `anim` number a state stands for, by its name ("Walk" = 2,
// "Land" = 1, "Crawl" = 7), or -1.
int StateNumber(const std::string& state) {
    if (state == "Land" || state == "GetUp") return 1;
    if (state == "Crawl" || state == "Drop") return 7;
    return Character::BuiltinAnimIndex(state);
}
} // namespace

Character* CharacterAnimBackend::Body() const {
    return m_owner ? m_owner->GetComponent<Character>() : nullptr;
}

const std::vector<std::string>& CharacterAnimBackend::Names() const {
    Character* ch = Body();
    if (!ch) { m_names.clear(); m_namesVer = ~0u; }
    else if (ch->ClipsVersion() != m_namesVer) {   // the body's clips, then the library's
        const std::vector<std::string> own = ch->ClipNames();
        const std::vector<std::string>& lib = HumanoidLibrary::Names();
        std::vector<std::string> old;
        old.swap(m_names);
        std::set_union(own.begin(), own.end(), lib.begin(), lib.end(), std::back_inserter(m_names));
        m_namesVer = ch->ClipsVersion();
        // What plays keeps playing: its clips by name in the new list.
        auto refind = [&](int& c) {
            if (c < 0 || c >= (int)old.size()) { c = -1; return; }
            auto it = std::lower_bound(m_names.begin(), m_names.end(), old[c]);
            c = (it != m_names.end() && *it == old[c]) ? (int)(it - m_names.begin()) : -1;
        };
        for (Playing* p : {&m_cur, &m_from}) {
            refind(p->clip);
            for (int& c : p->clips) refind(c);
        }
    }
    return m_names;
}

const AnimClip* CharacterAnimBackend::Clip(int i) const {
    const std::vector<std::string>& n = Names();
    Character* ch = Body();
    if (!ch || i < 0 || i >= (int)n.size()) return nullptr;
    const AnimClip* c = ch->FindClip(n[i]);
    return c ? c : HumanoidLibrary::Find(n[i]);
}

int CharacterAnimBackend::FindClip(const std::string& name) const {
    const std::vector<std::string>& n = Names();
    auto it = std::lower_bound(n.begin(), n.end(), name);   // ClipNames is sorted
    return (!name.empty() && it != n.end() && *it == name) ? (int)(it - n.begin()) : -1;
}

float CharacterAnimBackend::ClipLength(int clip) const {
    const AnimClip* c = Clip(clip);
    if (!c) return 0.0f;
    return c->Duration() / (c->speed > 1e-4f ? c->speed : 1.0f);   // in playback seconds
}

bool CharacterAnimBackend::ClipFinished() const {
    if (m_cur.blend || m_loop) return false;
    const float len = ClipLength(m_cur.clip);
    return len > 0.0f && m_cur.time >= len;
}

bool CharacterAnimBackend::Play(const std::string& name, float blend) {
    const int ci = FindClip(name);
    if (ci < 0) return false;
    Character* ch = Body();
    StartFade(blend >= 0.0f ? blend : (ch ? ch->blendTime : 0.0f));
    m_cur = Playing{};
    m_cur.clip = ci;
    m_cur.loop = m_loop;
    m_cur.rate = m_speed;
    m_fromStart = true;
    return true;
}

void CharacterAnimBackend::SetSpeed(float speed) {
    Character* ch = Body();
    m_speed = speed * (ch ? ch->animSpeed : 1.0f);
}

void CharacterAnimBackend::ResetBlendPhase(float p) {
    m_cur.phase = (m_carry && p == 0.0f) ? m_carryPhase : p;
    m_carry = false;
    m_fromStart = true;
}

void CharacterAnimBackend::SetTime(float t) {
    if (m_cur.blend) return;
    const float len = ClipLength(m_cur.clip);
    if (len > 0.0f) t = m_loop ? frac01(t / len) * len : std::fmin(std::fmax(t, 0.0f), len);
    m_cur.time = t;
}

void CharacterAnimBackend::CaptureCrossfade(int A, int B, float seconds) {
    CaptureCrossfadeMany({A, B}, seconds);
}

void CharacterAnimBackend::CaptureCrossfadeMany(const std::vector<int>& clips, float seconds) {
    m_carry = m_cur.blend && m_have;
    m_carryPhase = m_cur.phase;
    StartFade(seconds);
    m_cur = Playing{};
    m_cur.blend = true;
    m_cur.clips = clips;
    m_cur.weights.assign(clips.size(), clips.empty() ? 0.0f : 1.0f / (float)clips.size());
    m_cur.rate = m_speed;
}

bool CharacterAnimBackend::BlendClips(float dt, int A, int B, float w) {
    if (!Clip(A)) A = -1;
    if (!Clip(B)) B = -1;
    if (A < 0 && B < 0) return false;
    if (A < 0) { A = B; w = 1.0f; }
    if (B < 0) { B = A; w = 0.0f; }
    w = std::fmin(std::fmax(w, 0.0f), 1.0f);
    return BlendMany(dt, {A, B}, {1.0f - w, w});
}

bool CharacterAnimBackend::BlendMany(float dt, const std::vector<int>& clips, const std::vector<float>& weights) {
    Playing& p = m_cur;
    p.blend = true;
    p.clips.clear(); p.weights.clear();
    float sum = 0.0f, cycle = 0.0f;
    int dom = -1; float domW = 0.0f;
    for (std::size_t i = 0; i < clips.size() && i < weights.size(); ++i) {
        if (weights[i] <= 0.0f || !Clip(clips[i])) continue;
        p.clips.push_back(clips[i]); p.weights.push_back(weights[i]);
        sum += weights[i];
        cycle += ClipLength(clips[i]) * weights[i];
        if (weights[i] > domW) { domW = weights[i]; dom = clips[i]; }
    }
    if (sum <= 0.0f) return false;
    for (float& w : p.weights) w /= sum;
    p.cycle = cycle / sum;
    p.rate = m_speed;
    // One shared phase: every clip samples the same fraction of its cycle, so the
    // footfalls line up through the blend.
    const float prev = p.phase;
    p.phase = frac01(p.phase + dt * m_speed / std::fmax(p.cycle, 1e-4f));
    const float len = ClipLength(dom);
    if (m_speed > 0.0f && len > 0.0f) {   // the dominant clip's events over this frame's window
        if (p.phase < prev) { FireWindow(dom, prev * len, len); m_fromStart = true; FireWindow(dom, 0.0f, p.phase * len); }
        else FireWindow(dom, prev * len, p.phase * len);
    }
    return true;
}

void CharacterAnimBackend::FireWindow(int clip, float t0, float t1) {
    const AnimClip* c = Clip(clip);
    Character* ch = Body();
    if (!c || !ch) return;
    const float spd = c->speed > 1e-4f ? c->speed : 1.0f;
    for (const AnimEvent& e : c->events) {
        const float at = e.time / spd;   // clip time -> playback seconds
        if ((at > t0 || (m_fromStart && at >= t0)) && at <= t1) ch->EmitAnimEvent(e.name);
    }
    m_fromStart = false;
}

void CharacterAnimBackend::Step(Playing& p, float dt) const {
    if (p.blend) { p.phase = frac01(p.phase + dt * p.rate / std::fmax(p.cycle, 1e-4f)); return; }
    const float len = ClipLength(p.clip);
    p.time += dt * p.rate;
    if (len > 0.0f) p.time = p.loop ? frac01(p.time / len) * len : std::fmin(p.time, len);   // a one-shot fading out holds its end
}

void CharacterAnimBackend::StartFade(float seconds) {
    if (seconds <= 1e-3f || !m_have) { m_fadeDur = 0.0f; return; }
    if (m_fadeDur > 0.0f) {   // a fade is already running: fade out of where it got to
        m_fromPose = m_pose; m_fromRoot = m_root; m_fromLive = false;
    } else {                  // fade out of the old playback, still moving
        m_from = m_cur; m_fromLive = true;
    }
    m_fadeT = 0.0f;
    m_fadeDur = seconds;
}

void CharacterAnimBackend::Sample(const Playing& p, std::vector<Vec3>& pose, Vec3& root) const {
    pose.clear(); root = Vec3{0, 0, 0};
    if (!p.blend) {
        const AnimClip* c = Clip(p.clip);
        if (!c) return;
        const float t = p.time * (c->speed > 1e-4f ? c->speed : 1.0f);
        pose = c->Sample(t); root = c->SampleRoot(t);
    } else {
        float total = 0.0f;
        for (std::size_t i = 0; i < p.clips.size() && i < p.weights.size(); ++i) {
            const AnimClip* c = Clip(p.clips[i]);
            if (!c) continue;
            const float t = p.phase * c->Duration();
            MixIn(pose, root, total, c->Sample(t), c->SampleRoot(t), p.weights[i]);
        }
    }
    if (!pose.empty()) pose.resize(Character::BoneCount(), Vec3{0, 0, 0});
}

void CharacterAnimBackend::Advance(float dt) {
    // A single clip runs on its own clock (a blend tree's phase moved in BlendMany).
    if (!m_cur.blend) {
        const float len = ClipLength(m_cur.clip);
        const float prev = m_cur.time;
        m_cur.time += dt * m_speed;
        m_cur.rate = m_speed;
        if (len > 0.0f) {
            if (m_loop) {
                if (m_speed > 0.0f && m_cur.time >= len) {
                    FireWindow(m_cur.clip, prev, len);
                    m_fromStart = true;
                    m_cur.time = frac01(m_cur.time / len) * len;
                    FireWindow(m_cur.clip, 0.0f, m_cur.time);
                } else {
                    if (m_speed > 0.0f) FireWindow(m_cur.clip, prev, m_cur.time);
                    m_cur.time = frac01(m_cur.time / len) * len;
                }
            } else {
                m_cur.time = std::fmin(std::fmax(m_cur.time, 0.0f), len);
                if (m_speed > 0.0f && m_cur.time > prev) FireWindow(m_cur.clip, prev, m_cur.time);
            }
        }
    }
    std::vector<Vec3> pose; Vec3 root;
    Sample(m_cur, pose, root);
    if (m_fadeDur > 0.0f) {
        if (m_fromLive) Step(m_from, dt);
        m_fadeT += dt;
        std::vector<Vec3> from; Vec3 fromRoot = m_fromRoot;
        if (m_fromLive) Sample(m_from, from, fromRoot); else from = m_fromPose;
        if (m_fadeT >= m_fadeDur) m_fadeDur = 0.0f;
        else if (!pose.empty() && from.size() == pose.size()) {
            float u = m_fadeT / m_fadeDur;
            u = u * u * (3.0f - 2.0f * u);
            for (std::size_t b = 0; b < pose.size(); ++b) pose[b] = AnimClip::BlendEuler(from[b], pose[b], u);
            root = fromRoot + (root - fromRoot) * u;
        }
    }
    if (m_mirror && !pose.empty()) {
        Character::MirrorPose(pose);
        root.x = -root.x;
    }
    if (m_rootMotion == 0) root = Vec3{0, 0, 0};
    m_have = !pose.empty();
    m_pose = std::move(pose); m_root = root;
    m_final = false; m_dt = dt;
    Character* ch = Body();
    if (!ch) return;
    if (m_have && ch->GetPoseSource() != this) ch->SetPoseSource(this);
    // `anim` reads the state playing.
    if (m_sm && !m_off) {
        const int n = StateNumber(m_sm->Current());
        if (n > 0) ch->anim = n;
        m_published = ch->anim;
    }
}

void CharacterAnimBackend::BeginFrame(AnimStateMachine& sm) {
    m_sm = &sm;
    Character* ch = Body();
    if (!ch) return;
    const int a = ch->anim;
    if (a != m_published) {   // written from outside: go to that state and stay
        if (a == 0) m_off = true;
        else {
            const AnimStateMachine::State* to = nullptr;
            for (const auto& st : sm.states) if (StateNumber(st.name) == a) { to = &st; break; }
            if (to) {
                m_off = false;
                m_forced = true;
                if (to->name != sm.Current()) {
                    // The graph's own blend into that state, if it has one.
                    float blend = 0.2f;
                    const AnimStateMachine::State* cur = nullptr;
                    for (const auto& st : sm.states) if (st.name == sm.Current()) cur = &st;
                    bool found = false;
                    if (cur) for (const auto& tr : cur->transitions) if (tr.to == to->name) { blend = tr.blend; found = true; break; }
                    if (!found) for (const auto& tr : sm.anyState) if (tr.to == to->name) { blend = tr.blend; break; }
                    sm.GoTo(to->name, m_published < 0 ? 0.0f : blend);
                }
            }
        }
        m_published = a;
    } else if (m_moved) {
        m_forced = false;
    }
    m_moved = false;
    sm.hold = m_forced || ch->AnimHeld();
}

void CharacterAnimBackend::Move(const Movement& m) {
    m_moved = true;
    if (!m_sm) return;
    m_sm->SetFloat("speed", m.speed);
    m_sm->SetBool("run", m.run);
    m_sm->SetBool("crouch", m.crouch);
    m_sm->SetBool("prone", m.prone);
    m_sm->SetFloat("stance", m.stance);
    m_sm->SetFloat("airTime", m.airTime);
    m_sm->SetFloat("verticalSpeed", m.verticalSpeed);
}

void CharacterAnimBackend::Gait(float x, float z, float speed) {
    if (!m_sm) return;
    m_sm->SetFloat("gaitX", x);
    m_sm->SetFloat("gaitZ", z);
    m_sm->SetFloat("gaitSpeed", speed);
}

bool CharacterAnimBackend::Live() const {
    return m_have && !m_off && m_machine && m_machine->enabled;
}

bool CharacterAnimBackend::AnimatorPose(std::vector<Vec3>& pose, Vec3& root) {
    if (!m_have) return false;
    if (!m_final) {   // layers pose on top once a frame, after every Update ran
        m_final = true;
        for (std::size_t i = 0; i < m_post.size(); ++i) m_post[i].fn(m_dt);
    }
    pose = m_pose; root = m_root;
    return true;
}

void CharacterAnimBackend::PoseLayer(const std::vector<std::pair<int, float>>& clipTimes, const std::vector<float>& weights,
                                     const std::string& maskBone, float amount) {
    Character* ch = Body();
    if (!ch || !m_have || amount <= 0.0f) return;
    // The mask bone and every bone under it ("" = the whole body, root included).
    const std::vector<Character::Bone> sk = ch->Skeleton();
    std::vector<char> in(sk.size(), maskBone.empty() ? 1 : 0);
    if (!maskBone.empty()) {
        const int mb = Character::BoneIndex(maskBone);
        if (mb < 0) return;
        for (int b = 0; b < (int)sk.size(); ++b)
            for (int a = b; a >= 0; a = sk[a].parent) if (a == mb) { in[b] = 1; break; }
    }
    std::vector<Vec3> lp; Vec3 lr; float total = 0.0f;
    for (std::size_t k = 0; k < clipTimes.size() && k < weights.size(); ++k) {
        const AnimClip* c = Clip(clipTimes[k].first);
        if (!c) continue;
        const float t = clipTimes[k].second * (c->speed > 1e-4f ? c->speed : 1.0f);
        MixIn(lp, lr, total, c->Sample(t), c->SampleRoot(t), weights[k]);
    }
    if (lp.empty()) return;
    if (m_mirror) {
        Character::MirrorPose(lp);
        lr.x = -lr.x;
    }
    const float u = std::fmin(amount, 1.0f);
    for (std::size_t b = 0; b < m_pose.size() && b < lp.size() && b < in.size(); ++b)
        if (in[b]) m_pose[b] = AnimClip::BlendEuler(m_pose[b], lp[b], u);
    if (!in.empty() && in[0]) m_root = m_root + (lr - m_root) * u;   // the layer carries the hips
}

void CharacterAnimBackend::AddPostPose(const void* owner, std::function<void(float)> fn) {
    RemovePostPose(owner);
    m_post.push_back({owner, std::move(fn)});
}

void CharacterAnimBackend::RemovePostPose(const void* owner) {
    for (std::size_t i = 0; i < m_post.size(); ++i)
        if (m_post[i].owner == owner) { m_post.erase(m_post.begin() + (std::ptrdiff_t)i); return; }
}

void CharacterAnimBackend::FireEvent(const std::string& name) {
    if (Character* ch = Body()) ch->EmitAnimEvent(name);
}

void CharacterAnimBackend::Detach() {
    if (Character* ch = Body())
        if (ch->GetPoseSource() == this) ch->SetPoseSource(nullptr);
    m_have = false;
}

} // namespace okay
