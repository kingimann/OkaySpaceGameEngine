#include "okay/Components/Sequence.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"   // AssetBaseDir: project-relative nest paths
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Scene/EnsureCamera.hpp"   // PickRenderCamera: the camera the game renders through
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace okay {

namespace {

// ---- Text helpers ----------------------------------------------------------
// A line's tokens: whitespace-separated, a "quoted string" (with \" and \\) is
// one token, # starts a comment.
std::vector<std::string> Tokens(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, any = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '\\' && i + 1 < line.size()) { cur += line[++i]; continue; }
            if (c == '"') { quoted = false; out.push_back(cur); cur.clear(); continue; }
            cur += c;
            continue;
        }
        if (c == '#') break;
        if (c == '"') { if (any) { out.push_back(cur); cur.clear(); any = false; } quoted = true; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (any) { out.push_back(cur); cur.clear(); any = false; }
            continue;
        }
        cur += c; any = true;
    }
    if (quoted || any) out.push_back(cur);
    return out;
}

std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
    return out + "\"";
}

bool Num(const std::string& s, float& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtof(s.c_str(), &end);
    return end && *end == '\0';
}

std::string Fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%g", v);
    return b;
}
std::string Fmt3(const Vec3& v) { return Fmt(v.x) + " " + Fmt(v.y) + " " + Fmt(v.z); }

template <class T> float LastTime(const std::vector<T>& v, float last) {
    for (const T& e : v) last = std::max(last, e.time);
    return last;
}
template <class T> void SortByTime(std::vector<T>& v) {
    std::stable_sort(v.begin(), v.end(), [](const T& a, const T& b) { return a.time < b.time; });
}

float LerpFov(float a, float b, float u) {
    if (a <= 0.0f) return b;
    if (b <= 0.0f) return a;
    return a + (b - a) * u;
}

} // namespace

// ---- Sequence: timing -------------------------------------------------------
namespace {
float NestSpan(const Sequence& seq, const SeqLibrary* lib, int depth, std::vector<const Sequence*>& stack);
}

float Sequence::LastEntryTime(const SeqLibrary* lib) const {
    float last = LastTime(shots, 0.0f);
    for (const auto& t : transforms) { last = LastTime(t.position, last); last = LastTime(t.rotation, last); last = LastTime(t.scale, last); }
    for (const auto& t : animations) last = LastTime(t.cues, last);
    for (const auto& t : audio) last = LastTime(t.cues, last);
    for (const auto& t : events) last = LastTime(t.events, last);
    if (letterbox.on) last = LastTime(letterbox.keys, last);
    if (shake.on) { last = LastTime(shake.strength, last); last = LastTime(shake.frequency, last); }
    if (fade.on) last = LastTime(fade.keys, last);
    for (const auto& t : nests) last = LastTime(t.cues, last);
    if (!lib) return last;
    std::vector<const Sequence*> stack;
    return std::max(last, NestSpan(*this, lib, 0, stack));
}

float Sequence::Duration(const SeqLibrary* lib) const { return length > 0.0f ? length : LastEntryTime(lib); }

float Sequence::LoopEndTime() const {
    const float d = Duration();
    return (loopEnd > loopStart && loopEnd <= d) ? loopEnd : d;
}

void Sequence::Sort() {
    SortByTime(shots);
    for (auto& t : transforms) { SortByTime(t.position); SortByTime(t.rotation); SortByTime(t.scale); }
    for (auto& t : animations) SortByTime(t.cues);
    for (auto& t : audio) SortByTime(t.cues);
    for (auto& t : events) SortByTime(t.events);
    SortByTime(letterbox.keys);
    SortByTime(shake.strength); SortByTime(shake.frequency);
    SortByTime(fade.keys);
    for (auto& t : nests) SortByTime(t.cues);
}

// ---- Evaluation --------------------------------------------------------------
GameObject* Sequence::FindObject(const Scene& scene, const std::string& path) {
    if (path.empty()) return nullptr;
    const std::size_t slash = path.find('/');
    if (slash == std::string::npos) return scene.Find(path);
    GameObject* go = scene.Find(path.substr(0, slash));
    std::size_t at = slash + 1;
    while (go && at <= path.size()) {
        std::size_t next = path.find('/', at);
        if (next == std::string::npos) next = path.size();
        const std::string part = path.substr(at, next - at);
        GameObject* child = nullptr;
        if (go->transform)
            for (Transform* c : go->transform->Children())
                if (c && c->gameObject && c->gameObject->name == part) { child = c->gameObject; break; }
        go = child;
        at = next + 1;
    }
    return go;
}

SeqCameraPose Sequence::ShotPose(const SeqShot& shot, const Scene* scene, const SeqCameraPose& before,
                                 const std::string& drivenCamera) {
    SeqCameraPose p;
    if (!shot.camera.empty()) {
        if (!drivenCamera.empty() && shot.camera == drivenCamera) return before;
        GameObject* go = scene ? FindObject(*scene, shot.camera) : nullptr;
        if (!go || !go->transform) return before;
        p.valid = true;
        p.position = go->transform->Position();
        p.rotation = go->transform->Rotation();
        if (auto* cam = go->GetComponent<Camera>()) p.fov = cam->fieldOfView;
        return p;
    }
    p.valid = true;
    p.position = shot.position;
    p.rotation = Quat::Euler(shot.rotation);
    p.fov = shot.fov;
    return p;
}

SeqCameraPose Sequence::CameraAt(float t, const Scene* scene, const SeqCameraPose& before,
                                 const std::string& drivenCamera) const {
    int i = -1;
    for (std::size_t k = 0; k < shots.size(); ++k)
        if (shots[k].time <= t) i = (int)k; else break;
    if (i < 0) return before;
    const SeqShot& shot = shots[(std::size_t)i];
    SeqCameraPose to = ShotPose(shot, scene, before, drivenCamera);
    if (shot.blend <= 0.0f || t >= shot.time + shot.blend) return to;
    const SeqCameraPose from = i > 0 ? ShotPose(shots[(std::size_t)i - 1], scene, before, drivenCamera) : before;
    if (!from.valid) return to;
    const float u = Easing::Evaluate(shot.ease, Mathf::Clamp01((t - shot.time) / shot.blend));
    SeqCameraPose p;
    p.valid = true;
    p.position = Vec3::LerpUnclamped(from.position, to.position, u);
    p.rotation = Quat::Slerp(from.rotation, to.rotation, u);
    p.fov = LerpFov(from.fov, to.fov, Mathf::Clamp01(u));
    return p;
}

namespace {
// The two keys around `t` and the eased fraction between them (clamped to the ends).
template <class Key>
bool Span(const std::vector<Key>& keys, float t, const Key*& a, const Key*& b, float& u) {
    if (keys.empty()) return false;
    if (t <= keys.front().time || keys.size() == 1) { a = b = &keys.front(); u = 0.0f; return true; }
    if (t >= keys.back().time) { a = b = &keys.back(); u = 0.0f; return true; }
    std::size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].time <= t) ++i;
    a = &keys[i]; b = &keys[i + 1];
    const float span = b->time - a->time;
    u = span > 1e-6f ? Easing::Evaluate(a->ease, (t - a->time) / span) : 1.0f;
    return true;
}

Color LerpColor(const Color& a, const Color& b, float u) {
    return {a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u, a.a + (b.a - a.a) * u};
}

// `src` painted over `dst` (both straight alpha).
Color Over(const Color& dst, const Color& src) {
    const float sa = Mathf::Clamp01(src.a), da = Mathf::Clamp01(dst.a);
    const float outA = sa + da * (1.0f - sa);
    if (outA < 1e-5f) return {0, 0, 0, 0};
    return {(src.r * sa + dst.r * da * (1.0f - sa)) / outA,
            (src.g * sa + dst.g * da * (1.0f - sa)) / outA,
            (src.b * sa + dst.b * da * (1.0f - sa)) / outA, outA};
}

// Smooth repeatable noise in about -1..1. The same t always gives the same answer.
float ShakeNoise(float t, float freq, float seed) {
    const float x = t * (freq > 0.05f ? freq : 0.05f);
    return std::sin(x * 1.00f + seed) * 0.6f + std::sin(x * 1.73f + seed * 2.31f) * 0.4f;
}

bool Seen(const std::vector<const Sequence*>& stack, const Sequence* s) {
    for (const Sequence* p : stack) if (p == s) return true;
    return false;
}

// How far a nest cue runs on the parent's clock. Uses the child's own length
// (never Duration(lib), which would walk the nests again and loop on a cycle).
float NestSpan(const Sequence& seq, const SeqLibrary* lib, int depth, std::vector<const Sequence*>& stack) {
    if (!lib || depth > 8 || Seen(stack, &seq)) return 0.0f;
    stack.push_back(&seq);
    float last = 0.0f;
    for (const auto& tr : seq.nests)
        for (const auto& cue : tr.cues) {
            last = std::max(last, cue.time);
            const Sequence* child = lib->Find(cue.path);
            if (!child || Seen(stack, child)) continue;
            const float own = child->length > 0.0f ? child->length : child->LastEntryTime(nullptr);
            const float nested = NestSpan(*child, lib, depth + 1, stack);
            const float dur = std::max(own, nested);
            last = std::max(last, cue.time + std::max(0.0f, dur - cue.offset));
        }
    stack.pop_back();
    return last;
}

// True while `t` is inside the child's play, and `local` is the child's clock.
bool NestLive(const SeqNestCue& cue, const Sequence& child, float t, const SeqLibrary* lib, float& local) {
    local = t - cue.time + cue.offset;
    const float dur = child.Duration(lib);
    return t + 1e-4f >= cue.time && local + 1e-4f >= cue.offset && local <= dur + 1e-4f;
}
} // namespace

bool Sequence::ChannelAt(const std::vector<SeqKey>& keys, float t, Vec3& out) {
    const SeqKey* a; const SeqKey* b; float u;
    if (!Span(keys, t, a, b, u)) return false;
    out = Vec3::LerpUnclamped(a->value, b->value, u);
    return true;
}

bool Sequence::RotationAt(const std::vector<SeqKey>& keys, float t, Quat& out) {
    const SeqKey* a; const SeqKey* b; float u;
    if (!Span(keys, t, a, b, u)) return false;
    out = a == b ? Quat::Euler(a->value) : Quat::Slerp(Quat::Euler(a->value), Quat::Euler(b->value), u);
    return true;
}

bool Sequence::FloatAt(const std::vector<SeqFloatKey>& keys, float t, float& out) {
    const SeqFloatKey* a; const SeqFloatKey* b; float u;
    if (!Span(keys, t, a, b, u)) return false;
    out = a->value + (b->value - a->value) * u;
    return true;
}

bool Sequence::ColorAt(const std::vector<SeqColorKey>& keys, float t, Color& out) {
    const SeqColorKey* a; const SeqColorKey* b; float u;
    if (!Span(keys, t, a, b, u)) return false;
    out = LerpColor(a->color, b->color, u);
    return true;
}

namespace {
void OverlayLocal(const Sequence& seq, float t, SeqOverlay& o) {
    // Fades: each one eases the level from where the previous left it (at the
    // moment this one starts) to black (out) or clear (in).
    float start = -1.0f, from = 0.0f, to = 0.0f, dur = 0.0f;
    auto levelAt = [&](float x) {
        if (start < 0.0f) return 0.0f;
        const float u = dur > 1e-6f ? Mathf::Clamp01((x - start) / dur) : 1.0f;
        return from + (to - from) * Easing::SineInOut(u);
    };
    std::vector<const SeqEvent*> fades;
    const SeqEvent* sub = nullptr;
    for (const auto& tr : seq.events)
        for (const auto& e : tr.events) {
            if (e.time > t) continue;
            if (e.kind == SeqEvent::Kind::FadeOut || e.kind == SeqEvent::Kind::FadeIn) fades.push_back(&e);
            else if (e.kind == SeqEvent::Kind::Subtitle && t < e.time + e.value && (!sub || e.time >= sub->time)) sub = &e;
        }
    std::stable_sort(fades.begin(), fades.end(), [](const SeqEvent* a, const SeqEvent* b) { return a->time < b->time; });
    for (const SeqEvent* f : fades) {
        const float level = levelAt(f->time);
        start = f->time; from = level; dur = std::max(0.0f, f->value);
        to = f->kind == SeqEvent::Kind::FadeOut ? 1.0f : 0.0f;
    }
    o.fade = levelAt(t);
    if (sub) {
        o.speaker = sub->name; o.text = sub->text;
        const float edge = 0.2f;   // a short fade at both ends
        o.subtitle = Mathf::Clamp01(std::min((t - sub->time) / edge, (sub->time + sub->value - t) / edge));
    }
    if (seq.letterbox.on) {
        float amount = 0.0f;
        if (Sequence::FloatAt(seq.letterbox.keys, t, amount))
            o.letterbox = Mathf::Clamp(amount, 0.0f, 0.45f);
        o.letterboxColor = seq.letterbox.color;
    }
    if (seq.fade.on) Sequence::ColorAt(seq.fade.keys, t, o.screen);
}

void OverlayDeep(const Sequence& seq, float t, const SeqLibrary* lib, SeqOverlay& o, int depth,
                 std::vector<const Sequence*>& stack) {
    OverlayLocal(seq, t, o);
    if (!lib || depth > 8 || Seen(stack, &seq)) return;
    stack.push_back(&seq);
    for (const auto& tr : seq.nests)
        for (const auto& cue : tr.cues) {
            const Sequence* child = lib->Find(cue.path);
            if (!child) continue;
            float local = 0.0f;
            if (!NestLive(cue, *child, t, lib, local)) continue;
            SeqOverlay c;
            OverlayDeep(*child, local, lib, c, depth + 1, stack);
            if (c.letterbox > o.letterbox) { o.letterbox = c.letterbox; o.letterboxColor = c.letterboxColor; }
            o.screen = Over(o.screen, c.screen);
            o.fade = std::max(o.fade, c.fade);
            if (!c.text.empty()) { o.speaker = c.speaker; o.text = c.text; o.subtitle = c.subtitle; }
        }
    stack.pop_back();
}

void ShakeDeep(const Sequence& seq, float t, const SeqLibrary* lib, Vec3& pos, Vec3& rot, int depth,
               std::vector<const Sequence*>& stack) {
    if (seq.shake.on && Accessibility::MotionScale() > 0.0f) {
        float strength = 0.0f, freq = 18.0f;
        Sequence::FloatAt(seq.shake.strength, t, strength);
        if (!seq.shake.frequency.empty()) Sequence::FloatAt(seq.shake.frequency, t, freq);
        strength = Mathf::Clamp01(strength);
        if (strength > 1e-4f) {
            pos += Vec3{ShakeNoise(t, freq, 1.3f), ShakeNoise(t, freq, 7.9f), 0.0f} * strength;
            rot += Vec3{ShakeNoise(t, freq, 3.7f), ShakeNoise(t, freq, 5.1f), ShakeNoise(t, freq, 9.4f) * 0.5f} * strength;
        }
    }
    if (!lib || depth > 8 || Seen(stack, &seq)) return;
    stack.push_back(&seq);
    for (const auto& tr : seq.nests)
        for (const auto& cue : tr.cues) {
            const Sequence* child = lib->Find(cue.path);
            if (!child) continue;
            float local = 0.0f;
            if (!NestLive(cue, *child, t, lib, local)) continue;
            ShakeDeep(*child, local, lib, pos, rot, depth + 1, stack);
        }
    stack.pop_back();
}

bool InRange(float t, float a, float b, bool includeA) {
    return (t > a || (includeA && t >= a)) && t <= b;
}

void CollectDeep(const Sequence& seq, float a, float b, bool includeA, const SeqLibrary* lib,
                 std::vector<SeqFired>& out, int depth, std::vector<const Sequence*>& stack) {
    if (depth > 8 || Seen(stack, &seq)) return;
    stack.push_back(&seq);
    for (const auto& tr : seq.events)
        for (const auto& e : tr.events)
            if (InRange(e.time, a, b, includeA)) out.push_back(SeqFired{e.time, &e, nullptr, nullptr, nullptr, nullptr});
    for (const auto& tr : seq.animations)
        for (const auto& c : tr.cues)
            if (InRange(c.time, a, b, includeA)) out.push_back(SeqFired{c.time, nullptr, &tr, &c, nullptr, nullptr});
    for (const auto& tr : seq.audio)
        for (const auto& c : tr.cues)
            if (InRange(c.time, a, b, includeA)) out.push_back(SeqFired{c.time, nullptr, nullptr, nullptr, &tr, &c});
    if (lib) {
        for (const auto& tr : seq.nests)
            for (const auto& cue : tr.cues) {
                const Sequence* child = lib->Find(cue.path);
                if (!child) continue;
                const float dur = child->Duration(lib);
                const float enter = cue.time;
                const float exit = cue.time + std::max(0.0f, dur - cue.offset);
                const float left = std::max(a, enter);
                const float right = std::min(b, exit);
                if (right < left) continue;
                if (left == right && !InRange(left, a, b, includeA)) continue;
                const float la = left - cue.time + cue.offset;
                const float lb = right - cue.time + cue.offset;
                const bool childInclude = InRange(left, a, b, includeA);
                const std::size_t at = out.size();
                CollectDeep(*child, la, lb, childInclude, lib, out, depth + 1, stack);
                const float shift = cue.time - cue.offset;
                for (std::size_t i = at; i < out.size(); ++i) out[i].time += shift;
            }
    }
    stack.pop_back();
}
} // namespace

SeqOverlay Sequence::OverlayAt(float t, const SeqLibrary* lib) const {
    SeqOverlay o;
    std::vector<const Sequence*> stack;
    OverlayDeep(*this, t, lib, o, 0, stack);
    return o;
}

void Sequence::ShakeAt(float t, const SeqLibrary* lib, Vec3& outPos, Vec3& outRotDeg) const {
    outPos = Vec3{0, 0, 0};
    outRotDeg = Vec3{0, 0, 0};
    std::vector<const Sequence*> stack;
    ShakeDeep(*this, t, lib, outPos, outRotDeg, 0, stack);
}

void Sequence::CollectFired(float a, float b, bool includeA, const SeqLibrary* lib, std::vector<SeqFired>& out) const {
    std::vector<const Sequence*> stack;
    CollectDeep(*this, a, b, includeA, lib, out, 0, stack);
    std::stable_sort(out.begin(), out.end(), [](const SeqFired& x, const SeqFired& y) { return x.time < y.time; });
}

// ---- Editing helpers ---------------------------------------------------------
int Sequence::KeyAt(const std::vector<SeqKey>& keys, float t, float eps) {
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (std::fabs(keys[i].time - t) <= eps) return (int)i;
    return -1;
}

int Sequence::SetKey(std::vector<SeqKey>& keys, float t, const Vec3& value, float eps) {
    const int at = KeyAt(keys, t, eps);
    if (at >= 0) { keys[(std::size_t)at].value = value; return at; }
    SeqKey k; k.time = t; k.value = value;
    std::size_t i = 0;
    while (i < keys.size() && keys[i].time < t) ++i;
    keys.insert(keys.begin() + (std::ptrdiff_t)i, k);
    return (int)i;
}

int Sequence::FloatKeyAt(const std::vector<SeqFloatKey>& keys, float t, float eps) {
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (std::fabs(keys[i].time - t) <= eps) return (int)i;
    return -1;
}

int Sequence::ColorKeyAt(const std::vector<SeqColorKey>& keys, float t, float eps) {
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (std::fabs(keys[i].time - t) <= eps) return (int)i;
    return -1;
}

int Sequence::SetFloatKey(std::vector<SeqFloatKey>& keys, float t, float value, float eps) {
    const int at = FloatKeyAt(keys, t, eps);
    if (at >= 0) { keys[(std::size_t)at].value = value; return at; }
    SeqFloatKey k; k.time = t; k.value = value;
    std::size_t i = 0;
    while (i < keys.size() && keys[i].time < t) ++i;
    keys.insert(keys.begin() + (std::ptrdiff_t)i, k);
    return (int)i;
}

int Sequence::SetColorKey(std::vector<SeqColorKey>& keys, float t, const Color& color, float eps) {
    const int at = ColorKeyAt(keys, t, eps);
    if (at >= 0) { keys[(std::size_t)at].color = color; return at; }
    SeqColorKey k; k.time = t; k.color = color;
    std::size_t i = 0;
    while (i < keys.size() && keys[i].time < t) ++i;
    keys.insert(keys.begin() + (std::ptrdiff_t)i, k);
    return (int)i;
}

float Sequence::SnapToFrame(float t, int fps) {
    if (fps <= 0) return t;
    return std::round(t * (float)fps) / (float)fps;
}

// ---- Text --------------------------------------------------------------------
std::string Sequence::EaseWord(Ease e) {
    std::string w;
    for (const char* p = Easing::Name(e); *p; ++p)
        if (*p != ' ') w += (char)std::tolower(static_cast<unsigned char>(*p));
    return w;
}

bool Sequence::Parse(const std::string& text, Sequence& out, std::string* error) {
    Sequence s;
    enum class Sec { None, Camera, Transform, Animation, Audio, Events, Letterbox, Shake, Fade, Nest } sec = Sec::None;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& m) {
        if (error) *error = "line " + std::to_string(lineNo) + ": " + m;
        return false;
    };
    while (std::getline(in, line)) {
        ++lineNo;
        const std::vector<std::string> t = Tokens(line);
        if (t.empty()) continue;
        const std::string& k = t[0];
        auto str = [&](std::size_t i) { return i < t.size() ? t[i] : std::string(); };
        auto num = [&](std::size_t i, float& v) { return i < t.size() && Num(t[i], v); };
        auto vec = [&](std::size_t i, Vec3& v) { return num(i, v.x) && num(i + 1, v.y) && num(i + 2, v.z); };
        if (k == "okayseq") continue;
        if (k == "sequence") { s.name = str(1); continue; }
        if (k == "length") { if (!num(1, s.length)) return fail("length needs seconds"); continue; }
        if (k == "loop") {
            float on = 0.0f;
            if (!num(1, on)) return fail("loop needs 0 or 1");
            s.loop = on != 0.0f;
            num(2, s.loopStart); num(3, s.loopEnd);
            continue;
        }
        if (k == "camera") { s.cameraTrack = true; sec = Sec::Camera; continue; }
        if (k == "transform") { s.transforms.push_back({str(1), {}, {}, {}}); sec = Sec::Transform; continue; }
        if (k == "animation") { s.animations.push_back({str(1), {}}); sec = Sec::Animation; continue; }
        if (k == "audio") { s.audio.push_back({str(1), {}}); sec = Sec::Audio; continue; }
        if (k == "events") { SeqEventTrack tr; if (t.size() > 1) tr.name = t[1]; s.events.push_back(tr); sec = Sec::Events; continue; }
        if (k == "letterbox") { s.letterbox.on = true; sec = Sec::Letterbox; continue; }
        if (k == "shake") { s.shake.on = true; sec = Sec::Shake; continue; }
        if (k == "fade") { s.fade.on = true; sec = Sec::Fade; continue; }
        if (k == "nest") { SeqNestTrack tr; if (t.size() > 1) tr.name = t[1]; s.nests.push_back(tr); sec = Sec::Nest; continue; }

        if (k == "shot") {
            if (sec != Sec::Camera) return fail("shot outside the camera track");
            SeqShot sh;
            if (!num(1, sh.time)) return fail("shot needs a time");
            for (std::size_t i = 2; i < t.size();) {
                const std::string& w = t[i];
                if (w == "cam") { sh.camera = str(i + 1); i += 2; }
                else if (w == "pose") { if (!vec(i + 1, sh.position) || !vec(i + 4, sh.rotation)) return fail("pose needs x y z rx ry rz"); i += 7; }
                else if (w == "fov") { if (!num(i + 1, sh.fov)) return fail("fov needs degrees"); i += 2; }
                else if (w == "blend") { if (!num(i + 1, sh.blend)) return fail("blend needs seconds"); i += 2; }
                else if (w == "ease") { sh.ease = Easing::FromName(str(i + 1), Ease::SineInOut); i += 2; }
                else return fail("unknown shot word '" + w + "'");
            }
            s.shots.push_back(sh);
            continue;
        }
        if (k == "pos" || k == "rot" || k == "scale") {
            if (sec != Sec::Transform) return fail(k + " outside a transform track");
            SeqKey key;
            if (!num(1, key.time) || !vec(2, key.value)) return fail(k + " needs a time and x y z");
            if (t.size() > 5) key.ease = Easing::FromName(t[5], Ease::Linear);
            auto& tr = s.transforms.back();
            (k == "pos" ? tr.position : k == "rot" ? tr.rotation : tr.scale).push_back(key);
            continue;
        }
        if (k == "color") {
            if (sec != Sec::Letterbox) return fail("color outside the letterbox track");
            if (!num(1, s.letterbox.color.r) || !num(2, s.letterbox.color.g) || !num(3, s.letterbox.color.b))
                return fail("color needs r g b");
            s.letterbox.color.a = 1.0f;
            num(4, s.letterbox.color.a);
            continue;
        }
        if (k == "bar") {
            if (sec != Sec::Letterbox) return fail("bar outside the letterbox track");
            SeqFloatKey key;
            if (!num(1, key.time) || !num(2, key.value)) return fail("bar needs a time and a height");
            if (t.size() > 3) key.ease = Easing::FromName(t[3], Ease::Linear);
            s.letterbox.keys.push_back(key);
            continue;
        }
        if (k == "strength" || k == "frequency") {
            if (sec != Sec::Shake) return fail(k + " outside the shake track");
            SeqFloatKey key;
            if (!num(1, key.time) || !num(2, key.value)) return fail(k + " needs a time and a number");
            if (t.size() > 3) key.ease = Easing::FromName(t[3], Ease::Linear);
            (k == "strength" ? s.shake.strength : s.shake.frequency).push_back(key);
            continue;
        }
        if (k == "rgba") {
            if (sec != Sec::Fade) return fail("rgba outside the fade track");
            SeqColorKey key;
            if (!num(1, key.time) || !num(2, key.color.r) || !num(3, key.color.g) || !num(4, key.color.b) || !num(5, key.color.a))
                return fail("rgba needs a time, a colour and an opacity");
            if (t.size() > 6) key.ease = Easing::FromName(t[6], Ease::Linear);
            s.fade.keys.push_back(key);
            continue;
        }
        if (k == "play" && sec == Sec::Nest) {
            SeqNestCue c;
            if (!num(1, c.time)) return fail("play needs a time");
            c.path = str(2);
            num(3, c.offset);
            s.nests.back().cues.push_back(c);
            continue;
        }
        if (k == "play" || k == "stop" || k == "float" || k == "bool" || k == "trigger" || k == "state") {
            if (sec != Sec::Animation) return fail(k + " outside an animation track");
            SeqAnimCue c;
            if (!num(1, c.time)) return fail(k + " needs a time");
            using K = SeqAnimCue::Kind;
            c.kind = k == "play" ? K::Play : k == "stop" ? K::Stop : k == "float" ? K::SetFloat
                   : k == "bool" ? K::SetBool : k == "trigger" ? K::Trigger : K::GoToState;
            if (c.kind != K::Stop) c.name = str(2);
            if (c.kind == K::SetFloat || c.kind == K::SetBool) num(3, c.value);
            s.animations.back().cues.push_back(c);
            continue;
        }
        if (k == "cue") {
            if (sec != Sec::Audio) return fail("cue outside an audio track");
            SeqAudioCue c;
            if (!num(1, c.time)) return fail("cue needs a time");
            c.file = str(2);
            num(3, c.volume);
            s.audio.back().cues.push_back(c);
            continue;
        }
        using E = SeqEvent::Kind;
        const E kind = k == "event" ? E::Named : k == "active" ? E::SetActive : k == "teleport" ? E::Teleport
                     : k == "fadeout" ? E::FadeOut : k == "fadein" ? E::FadeIn : k == "subtitle" ? E::Subtitle
                     : k == "timescale" ? E::TimeScale : k == "wait" ? E::WaitForInput : k == "end" ? E::End
                     : E::Named;
        if (kind == E::Named && k != "event") return fail("unknown line '" + k + "'");
        if (sec != Sec::Events) return fail(k + " outside an events track");
        SeqEvent e;
        e.kind = kind;
        if (!num(1, e.time)) return fail(k + " needs a time");
        switch (kind) {
        case E::Named:        e.name = str(2); break;
        case E::SetActive:    e.name = str(2); num(3, e.value); break;
        case E::Teleport:     e.name = str(2); e.text = str(3); break;
        case E::FadeOut:
        case E::FadeIn:       num(2, e.value); break;
        case E::Subtitle:     e.name = str(2); e.text = str(3); num(4, e.value); break;
        case E::TimeScale:    e.value = 1.0f; num(2, e.value); break;
        case E::WaitForInput:
        case E::End:          break;
        }
        s.events.back().events.push_back(e);
    }
    s.Sort();
    out = std::move(s);
    return true;
}

std::string Sequence::Write() const {
    std::ostringstream o;
    o << "okayseq 1\n";
    o << "sequence " << Quote(name) << "\n";
    if (length > 0.0f) o << "length " << Fmt(length) << "\n";
    if (loop || loopStart > 0.0f || loopEnd > 0.0f)
        o << "loop " << (loop ? 1 : 0) << " " << Fmt(loopStart) << " " << Fmt(loopEnd) << "\n";
    if (cameraTrack) {
        o << "camera\n";
        for (const SeqShot& sh : shots) {
            o << "  shot " << Fmt(sh.time);
            if (!sh.camera.empty()) o << " cam " << Quote(sh.camera);
            else o << " pose " << Fmt3(sh.position) << " " << Fmt3(sh.rotation);
            if (sh.fov > 0.0f) o << " fov " << Fmt(sh.fov);
            o << " blend " << Fmt(sh.blend) << " ease " << EaseWord(sh.ease) << "\n";
        }
    }
    if (letterbox.on) {
        o << "letterbox\n";
        o << "  color " << Fmt(letterbox.color.r) << " " << Fmt(letterbox.color.g) << " " << Fmt(letterbox.color.b)
          << " " << Fmt(letterbox.color.a) << "\n";
        for (const SeqFloatKey& k : letterbox.keys)
            o << "  bar " << Fmt(k.time) << " " << Fmt(k.value) << " " << EaseWord(k.ease) << "\n";
    }
    if (shake.on) {
        o << "shake\n";
        for (const SeqFloatKey& k : shake.strength)
            o << "  strength " << Fmt(k.time) << " " << Fmt(k.value) << " " << EaseWord(k.ease) << "\n";
        for (const SeqFloatKey& k : shake.frequency)
            o << "  frequency " << Fmt(k.time) << " " << Fmt(k.value) << " " << EaseWord(k.ease) << "\n";
    }
    if (fade.on) {
        o << "fade\n";
        for (const SeqColorKey& k : fade.keys)
            o << "  rgba " << Fmt(k.time) << " " << Fmt(k.color.r) << " " << Fmt(k.color.g) << " " << Fmt(k.color.b)
              << " " << Fmt(k.color.a) << " " << EaseWord(k.ease) << "\n";
    }
    for (const auto& tr : transforms) {
        o << "transform " << Quote(tr.object) << "\n";
        auto chan = [&](const char* word, const std::vector<SeqKey>& keys) {
            for (const SeqKey& k : keys) o << "  " << word << " " << Fmt(k.time) << " " << Fmt3(k.value) << " " << EaseWord(k.ease) << "\n";
        };
        chan("pos", tr.position); chan("rot", tr.rotation); chan("scale", tr.scale);
    }
    for (const auto& tr : animations) {
        o << "animation " << Quote(tr.object) << "\n";
        for (const SeqAnimCue& c : tr.cues) {
            using K = SeqAnimCue::Kind;
            o << "  ";
            switch (c.kind) {
            case K::Play:      o << "play " << Fmt(c.time) << " " << Quote(c.name); break;
            case K::Stop:      o << "stop " << Fmt(c.time); break;
            case K::SetFloat:  o << "float " << Fmt(c.time) << " " << Quote(c.name) << " " << Fmt(c.value); break;
            case K::SetBool:   o << "bool " << Fmt(c.time) << " " << Quote(c.name) << " " << (c.value != 0.0f ? 1 : 0); break;
            case K::Trigger:   o << "trigger " << Fmt(c.time) << " " << Quote(c.name); break;
            case K::GoToState: o << "state " << Fmt(c.time) << " " << Quote(c.name); break;
            }
            o << "\n";
        }
    }
    for (const auto& tr : audio) {
        o << "audio " << Quote(tr.object) << "\n";
        for (const SeqAudioCue& c : tr.cues) o << "  cue " << Fmt(c.time) << " " << Quote(c.file) << " " << Fmt(c.volume) << "\n";
    }
    for (const auto& tr : events) {
        o << "events " << Quote(tr.name) << "\n";
        for (const SeqEvent& e : tr.events) {
            using E = SeqEvent::Kind;
            o << "  ";
            switch (e.kind) {
            case E::Named:        o << "event " << Fmt(e.time) << " " << Quote(e.name); break;
            case E::SetActive:    o << "active " << Fmt(e.time) << " " << Quote(e.name) << " " << (e.value != 0.0f ? 1 : 0); break;
            case E::Teleport:     o << "teleport " << Fmt(e.time) << " " << Quote(e.name) << " " << Quote(e.text); break;
            case E::FadeOut:      o << "fadeout " << Fmt(e.time) << " " << Fmt(e.value); break;
            case E::FadeIn:       o << "fadein " << Fmt(e.time) << " " << Fmt(e.value); break;
            case E::Subtitle:     o << "subtitle " << Fmt(e.time) << " " << Quote(e.name) << " " << Quote(e.text) << " " << Fmt(e.value); break;
            case E::TimeScale:    o << "timescale " << Fmt(e.time) << " " << Fmt(e.value); break;
            case E::WaitForInput: o << "wait " << Fmt(e.time); break;
            case E::End:          o << "end " << Fmt(e.time); break;
            }
            o << "\n";
        }
    }
    for (const auto& tr : nests) {
        o << "nest " << Quote(tr.name) << "\n";
        for (const SeqNestCue& c : tr.cues)
            o << "  play " << Fmt(c.time) << " " << Quote(c.path) << " " << Fmt(c.offset) << "\n";
    }
    return o.str();
}

bool Sequence::LoadFromFile(const std::string& path, Sequence& out, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot read " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return Parse(ss.str(), out, error);
}

bool Sequence::SaveToFile(const std::string& path, std::string* error) const {
    std::ofstream f(path, std::ios::trunc);
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    f << Write();
    return true;
}

// ---- SequenceBinding -----------------------------------------------------------
namespace {
Camera* RenderCamera(Scene& scene) {
    std::vector<Camera*> cams;
    for (const auto& up : scene.Objects())
        if (up) if (auto* c = up->GetComponent<Camera>()) cams.push_back(c);
    return PickRenderCamera(scene.mainCamera, cams);
}
} // namespace

void SequenceBinding::Unbind() {
    if (Camera* cam = camera()) cam->ClearCutsceneShake();
    m_scene = nullptr;
    m_objects.clear();
    m_camera.clear();
}

void SequenceBinding::Bind(Scene& scene, const Sequence& seq, const SeqLibrary* lib) {
    Unbind();
    m_scene = &scene;
    std::vector<const Sequence*> stack;
    std::vector<std::string> paths;
    auto gather = [&](auto&& self, const Sequence& s, int depth) -> void {
        if (depth > 8) return;
        for (const Sequence* p : stack) if (p == &s) return;
        stack.push_back(&s);
        for (const auto& tr : s.transforms) if (!tr.object.empty()) paths.push_back(tr.object);
        if (lib)
            for (const auto& n : s.nests)
                for (const auto& c : n.cues)
                    if (const Sequence* child = lib->Find(c.path)) self(self, *child, depth + 1);
        stack.pop_back();
    };
    gather(gather, seq, 0);
    for (const std::string& path : paths) {
        bool known = false;
        for (const Saved& s : m_objects) if (s.path == path) known = true;
        if (known) continue;
        GameObject* go = Sequence::FindObject(scene, path);
        if (!go || !go->transform) continue;
        m_objects.push_back({path, go->transform->localPosition, go->transform->localRotation, go->transform->localScale});
    }
    if (Camera* cam = RenderCamera(scene); cam && cam->gameObject && cam->transform) {
        m_camera = cam->gameObject->name;
        m_camPos = cam->transform->localPosition;
        m_camRot = cam->transform->localRotation;
        m_camFov = cam->fieldOfView;
        m_before.valid = true;
        m_before.position = cam->transform->Position();
        m_before.rotation = cam->transform->Rotation();
        m_before.fov = cam->fieldOfView;
    }
}

Camera* SequenceBinding::camera() const {
    if (!m_scene || m_camera.empty()) return nullptr;
    GameObject* go = m_scene->Find(m_camera);
    return go ? go->GetComponent<Camera>() : nullptr;
}

namespace {
void ApplyMotion(Scene& scene, const Sequence& seq, float t, const SeqLibrary* lib,
                 const SeqCameraPose& before, const std::string& driven, Camera* cam, int depth,
                 std::vector<const Sequence*>& stack) {
    if (depth > 8) return;
    for (const Sequence* p : stack) if (p == &seq) return;
    stack.push_back(&seq);
    for (const auto& tr : seq.transforms) {
        GameObject* go = Sequence::FindObject(scene, tr.object);
        if (!go || !go->transform) continue;
        Vec3 v; Quat q;
        if (Sequence::ChannelAt(tr.position, t, v)) go->transform->localPosition = v;
        if (Sequence::RotationAt(tr.rotation, t, q)) go->transform->localRotation = q;
        if (Sequence::ChannelAt(tr.scale, t, v)) go->transform->localScale = v;
    }
    if (seq.cameraTrack && !seq.shots.empty() && cam && cam->transform) {
        const SeqCameraPose pose = seq.CameraAt(t, &scene, before, driven);
        if (pose.valid) {
            cam->transform->SetPosition(pose.position);
            cam->transform->SetRotation(pose.rotation);
            if (pose.fov > 0.0f) cam->fieldOfView = pose.fov;
        }
    }
    if (lib) {
        SeqCameraPose live = before;
        if (cam && cam->transform) {
            live.valid = true;
            live.position = cam->transform->Position();
            live.rotation = cam->transform->Rotation();
            live.fov = cam->fieldOfView;
        }
        for (const auto& n : seq.nests)
            for (const auto& cue : n.cues) {
                const Sequence* child = lib->Find(cue.path);
                if (!child) continue;
                float local = 0.0f;
                if (!NestLive(cue, *child, t, lib, local)) continue;
                ApplyMotion(scene, *child, local, lib, live, driven, cam, depth + 1, stack);
                if (cam && cam->transform) {
                    live.position = cam->transform->Position();
                    live.rotation = cam->transform->Rotation();
                    live.fov = cam->fieldOfView;
                }
            }
    }
    stack.pop_back();
}
} // namespace

void SequenceBinding::Apply(const Sequence& seq, float t, const SeqLibrary* lib) {
    if (!m_scene) return;
    // A nested sequence that is not playing right now must not leave an object
    // where the last scrub put it. Objects the parent itself keys are left alone.
    if (lib) {
        auto owned = [&](const std::string& path) {
            for (const auto& tr : seq.transforms) if (tr.object == path) return true;
            return false;
        };
        auto putBack = [&](const std::string& path) {
            if (path.empty() || owned(path)) return;
            for (const Saved& s : m_objects) {
                if (s.path != path) continue;
                GameObject* go = Sequence::FindObject(*m_scene, path);
                if (!go || !go->transform) return;
                go->transform->localPosition = s.position;
                go->transform->localRotation = s.rotation;
                go->transform->localScale = s.scale;
                return;
            }
        };
        auto restoreTree = [&](auto&& self, const Sequence& node, int depth) -> void {
            if (depth > 8) return;
            for (const auto& tr : node.transforms) putBack(tr.object);
            for (const auto& n : node.nests)
                for (const auto& cue : n.cues)
                    if (const Sequence* child = lib->Find(cue.path)) self(self, *child, depth + 1);
        };
        auto dormant = [&](auto&& self, const Sequence& node, float time, int depth, std::vector<const Sequence*>& stack) -> void {
            if (depth > 8) return;
            for (const Sequence* p : stack) if (p == &node) return;
            stack.push_back(&node);
            for (const auto& n : node.nests)
                for (const auto& cue : n.cues) {
                    const Sequence* child = lib->Find(cue.path);
                    if (!child) continue;
                    float local = 0.0f;
                    if (!NestLive(cue, *child, time, lib, local)) restoreTree(restoreTree, *child, depth + 1);
                    else self(self, *child, local, depth + 1, stack);
                }
            stack.pop_back();
        };
        std::vector<const Sequence*> stack;
        dormant(dormant, seq, t, 0, stack);
    }
    Camera* cam = camera();
    std::vector<const Sequence*> stack;
    ApplyMotion(*m_scene, seq, t, lib, m_before, m_camera, cam, 0, stack);
    if (!cam) return;
    Vec3 pos, rot;
    seq.ShakeAt(t, lib, pos, rot);
    if (pos.SqrMagnitude() + rot.SqrMagnitude() < 1e-10f) cam->ClearCutsceneShake();
    else cam->SetCutsceneShake(pos * cam->shakeAmount, rot * cam->shakeAngle);
}

void SequenceBinding::RestoreCamera() {
    Camera* cam = camera();
    if (!cam || !cam->transform) return;
    cam->ClearCutsceneShake();
    cam->transform->localPosition = m_camPos;
    cam->transform->localRotation = m_camRot;
    cam->fieldOfView = m_camFov;
}

// ---- Nested sequences -----------------------------------------------------------------
void SeqLibrary::Add(const std::string& key, Sequence seq) {
    for (auto& it : m_items)
        if (it.first == key) { it.second = std::move(seq); return; }
    m_items.emplace_back(key, std::move(seq));
}

const Sequence* SeqLibrary::Find(const std::string& key) const {
    if (key.empty()) return nullptr;
    for (const auto& it : m_items) if (it.first == key) return &it.second;
    namespace fs = std::filesystem;
    const std::string file = fs::path(key).filename().string();
    const std::string stem = fs::path(key).stem().string();
    for (const auto& it : m_items) {
        const fs::path p(it.first);
        if ((!file.empty() && p.filename() == file) || (!stem.empty() && p.stem() == stem)) return &it.second;
    }
    return nullptr;
}

namespace {
std::string Canon(const std::filesystem::path& p) {
    std::error_code ec;
    const std::filesystem::path c = std::filesystem::weakly_canonical(p, ec);
    return (ec || c.empty()) ? p.lexically_normal().generic_string() : c.generic_string();
}

void LoadNest(SeqLibrary& lib, const Sequence& seq, const std::string& fromFile, int depth,
              std::vector<std::string>& stack, std::string* note) {
    if (depth > 8) return;
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(fromFile).parent_path();
    const std::string& asset = Character::AssetBaseDir();
    for (const auto& tr : seq.nests)
        for (const auto& cue : tr.cues) {
            if (cue.path.empty() || lib.Find(cue.path)) continue;
            std::error_code ec;
            fs::path cand(cue.path);
            if (!cand.is_absolute() || !fs::exists(cand, ec)) {
                if (!dir.empty() && fs::exists(dir / cue.path, ec)) cand = dir / cue.path;
                else if (!asset.empty() && fs::exists(fs::path(asset) / cue.path, ec)) cand = fs::path(asset) / cue.path;
            }
            const std::string key = Canon(cand);
            bool cycle = false;
            for (const std::string& s : stack) if (s == key) cycle = true;
            if (cycle) {
                if (note && note->empty()) *note = "sequence \"" + cue.path + "\" nests itself, so that cue is skipped";
                continue;
            }
            Sequence child;
            std::string err;
            if (!Sequence::LoadFromFile(cand.string(), child, &err)) {
                if (note && note->empty()) *note = err.empty() ? "cannot read " + cue.path : err;
                continue;
            }
            stack.push_back(key);
            LoadNest(lib, child, cand.string(), depth + 1, stack, note);
            stack.pop_back();
            lib.Add(cue.path, std::move(child));
        }
}
} // namespace

void SeqLibrary::LoadFrom(const Sequence& root, const std::string& fromFile, std::string* note) {
    std::vector<std::string> stack;
    stack.push_back(Canon(fromFile));
    LoadNest(*this, root, fromFile, 0, stack, note);
}

void SequenceBinding::RestoreAll() {
    RestoreCamera();
    if (!m_scene) return;
    for (const Saved& s : m_objects) {
        GameObject* go = Sequence::FindObject(*m_scene, s.path);
        if (!go || !go->transform) continue;
        go->transform->localPosition = s.position;
        go->transform->localRotation = s.rotation;
        go->transform->localScale = s.scale;
    }
}

} // namespace okay
