#include "okay/Components/Sequence.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Scene/EnsureCamera.hpp"   // PickRenderCamera: the camera the game renders through
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
float Sequence::LastEntryTime() const {
    float last = LastTime(shots, 0.0f);
    for (const auto& t : transforms) { last = LastTime(t.position, last); last = LastTime(t.rotation, last); last = LastTime(t.scale, last); }
    for (const auto& t : animations) last = LastTime(t.cues, last);
    for (const auto& t : audio) last = LastTime(t.cues, last);
    for (const auto& t : events) last = LastTime(t.events, last);
    return last;
}

float Sequence::Duration() const { return length > 0.0f ? length : LastEntryTime(); }

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
bool Span(const std::vector<SeqKey>& keys, float t, const SeqKey*& a, const SeqKey*& b, float& u) {
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

SeqOverlay Sequence::OverlayAt(float t) const {
    SeqOverlay o;
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
    for (const auto& tr : events)
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
    return o;
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
    enum class Sec { None, Camera, Transform, Animation, Audio, Events } sec = Sec::None;
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

void SequenceBinding::Bind(Scene& scene, const Sequence& seq) {
    Unbind();
    m_scene = &scene;
    for (const auto& tr : seq.transforms) {
        GameObject* go = Sequence::FindObject(scene, tr.object);
        if (!go || !go->transform) continue;
        bool known = false;
        for (const Saved& s : m_objects) if (s.path == tr.object) known = true;
        if (known) continue;
        m_objects.push_back({tr.object, go->transform->localPosition, go->transform->localRotation, go->transform->localScale});
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

void SequenceBinding::Apply(const Sequence& seq, float t) {
    if (!m_scene) return;
    for (const auto& tr : seq.transforms) {
        GameObject* go = Sequence::FindObject(*m_scene, tr.object);
        if (!go || !go->transform) continue;
        Vec3 v; Quat q;
        if (Sequence::ChannelAt(tr.position, t, v)) go->transform->localPosition = v;
        if (Sequence::RotationAt(tr.rotation, t, q)) go->transform->localRotation = q;
        if (Sequence::ChannelAt(tr.scale, t, v)) go->transform->localScale = v;
    }
    if (!seq.cameraTrack || seq.shots.empty()) return;
    Camera* cam = camera();
    if (!cam || !cam->transform) return;
    const SeqCameraPose pose = seq.CameraAt(t, m_scene, m_before, m_camera);
    if (!pose.valid) return;
    cam->transform->SetPosition(pose.position);
    cam->transform->SetRotation(pose.rotation);
    if (pose.fov > 0.0f) cam->fieldOfView = pose.fov;
}

void SequenceBinding::RestoreCamera() {
    Camera* cam = camera();
    if (!cam || !cam->transform) return;
    cam->transform->localPosition = m_camPos;
    cam->transform->localRotation = m_camRot;
    cam->fieldOfView = m_camFov;
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
