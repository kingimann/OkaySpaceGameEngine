#include "okay/Components/SequencePlayer.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/AnimControl.hpp"        // PlayClipFrom, FindAnimComponent
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Character.hpp"          // AssetBaseDir: the project folder relative paths resolve under
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <filesystem>

namespace okay {

namespace {
std::string s_lastEvent;   // the event being delivered (Sequence.lastEvent in C#-style scripts)

// A playable entry of any track, so one pass fires everything in time order.
struct Due {
    float time;
    int kind;              // 0 event, 1 animation cue, 2 audio cue
    const SeqEvent* event;
    const SeqAnimTrack* animTrack; const SeqAnimCue* anim;
    const SeqAudioTrack* audioTrack; const SeqAudioCue* audio;
};
} // namespace

const std::string& SequencePlayer::LastEvent() { return s_lastEvent; }

// ---- Loading --------------------------------------------------------------------
std::string SequencePlayer::ResolvedPath() const {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(sequencePath);
    const std::string& base = Character::AssetBaseDir();
    if (sequencePath.empty() || p.is_absolute() || base.empty() || fs::exists(p, ec)) return sequencePath;
    return (fs::path(base) / p).string();
}

bool SequencePlayer::Load(std::string* error) {
    if (sequencePath.empty()) { if (error) *error = "no sequence file set"; return false; }
    Sequence s;
    if (!Sequence::LoadFromFile(ResolvedPath(), s, error)) return false;
    m_seq = std::move(s);
    m_loaded = true;
    m_loadedPath = sequencePath;
    return true;
}

// ---- Playback -------------------------------------------------------------------
bool SequencePlayer::Play() {
    if (!m_loaded || m_loadedPath != sequencePath) {
        std::string err;
        if (!Load(&err)) { Log::Warning("[sequence] ", err); return false; }
    }
    Scene* s = GetScene();
    if (!s) return false;
    if (m_playing) m_binding.RestoreCamera();   // a restart blends from the camera's own pose again
    m_binding.Bind(*s, m_seq);
    m_playing = true; m_paused = false; m_waiting = false;
    m_time = 0.0f;
    m_last = -1.0f;                 // the first step fires what sits at 0
    m_savedTimeScale = Time::TimeScale();
    m_changedTimeScale = false;
    m_binding.Apply(m_seq, 0.0f);
    if (onStarted) onStarted();
    Deliver("on_sequence_started", nullptr, m_seq.name);
    return true;
}

bool SequencePlayer::Play(const std::string& path) {
    sequencePath = path;
    return Play();
}

void SequencePlayer::Finish() {
    if (!m_playing) return;
    m_playing = false; m_paused = false; m_waiting = false;
    m_binding.RestoreCamera();
    if (m_changedTimeScale) { Time::SetTimeScale(m_savedTimeScale); m_changedTimeScale = false; }
    if (onFinished) onFinished();
    Deliver("on_sequence_finished", nullptr, m_seq.name);
}

void SequencePlayer::Stop() { Finish(); }

void SequencePlayer::Skip() {
    if (!m_playing) return;
    const float end = m_seq.Duration();
    m_waiting = false;
    m_skipping = true;
    FireBetween(m_last, end, false);
    m_skipping = false;
    m_time = end;
    m_last = end;
    if (m_playing) m_binding.Apply(m_seq, end);   // an "end" action may have finished it already
    Finish();
}

void SequencePlayer::Seek(float t) {
    m_time = Mathf::Clamp(t, 0.0f, m_seq.Duration());
    m_last = m_time;
    m_waiting = false;
    if (m_playing) m_binding.Apply(m_seq, m_time);
}

void SequencePlayer::Evaluate(float t) {
    if (!m_binding.Bound()) { Scene* s = GetScene(); if (!s) return; m_binding.Bind(*s, m_seq); }
    m_binding.Apply(m_seq, t);
}

SeqOverlay SequencePlayer::Overlay() const {
    if (!m_playing) return {};
    SeqOverlay o = m_seq.OverlayAt(m_time);
    o.waiting = m_waiting;
    return o;
}

bool SequencePlayer::ConfirmPressed() const {
    return Input::GetKeyDown('\r') || Input::GetKeyDown(' ') || Input::GetGamepadButtonDown(0);
}

void SequencePlayer::Step(float dt) {
    if (!m_playing || m_paused) return;
    if (m_waiting) {
        if (!ConfirmPressed()) return;
        m_waiting = false;
    }
    const bool looping = loop || m_seq.loop;
    const float end = looping ? m_seq.LoopEndTime() : m_seq.Duration();
    const float next = m_time + dt * timeScale;
    FireBetween(m_last, std::min(next, end), false);
    if (!m_playing) return;                       // an "end" action or a script stopped it
    if (m_waiting) { m_binding.Apply(m_seq, m_time); return; }
    if (next >= end) {
        if (looping && end > m_seq.loopStart) {
            float over = next - end;
            const float span = end - m_seq.loopStart;
            if (over >= span) over = std::fmod(over, span);
            m_time = m_seq.loopStart + over;
            FireBetween(m_seq.loopStart, m_time, true);   // what sits at the loop start fires again
            if (!m_playing) return;
            m_last = m_time;
            m_binding.Apply(m_seq, m_time);
            return;
        }
        m_time = end; m_last = end;
        m_binding.Apply(m_seq, end);
        Finish();
        return;
    }
    m_time = next; m_last = next;
    m_binding.Apply(m_seq, m_time);
}

// Fire everything with a time in (a, b] (or [a, b] with `includeA`), in time
// order across the tracks. A wait-for-input holds the playhead there: the rest
// of that instant still fires, later entries wait for the confirm key.
void SequencePlayer::FireBetween(float a, float b, bool includeA) {
    std::vector<Due> due;
    auto inRange = [&](float t) { return (t > a || (includeA && t >= a)) && t <= b; };
    for (const auto& tr : m_seq.events)
        for (const auto& e : tr.events) if (inRange(e.time)) due.push_back({e.time, 0, &e, nullptr, nullptr, nullptr, nullptr});
    for (const auto& tr : m_seq.animations)
        for (const auto& c : tr.cues) if (inRange(c.time)) due.push_back({c.time, 1, nullptr, &tr, &c, nullptr, nullptr});
    for (const auto& tr : m_seq.audio)
        for (const auto& c : tr.cues) if (inRange(c.time)) due.push_back({c.time, 2, nullptr, nullptr, nullptr, &tr, &c});
    std::stable_sort(due.begin(), due.end(), [](const Due& x, const Due& y) { return x.time < y.time; });
    Scene* s = GetScene();
    float holdAt = -1.0f;
    for (const Due& d : due) {
        if (!m_playing) return;
        if (holdAt >= 0.0f && d.time > holdAt) break;   // held on a wait: later entries fire after the confirm
        m_last = d.time;
        if (d.kind == 0) {
            Fire(*d.event);
            if (m_waiting && holdAt < 0.0f) { holdAt = d.time; m_time = d.time; }
        } else if (d.kind == 1) {
            GameObject* go = s ? Sequence::FindObject(*s, d.animTrack->object) : nullptr;
            if (!go) continue;
            using K = SeqAnimCue::Kind;
            const SeqAnimCue& c = *d.anim;
            if (c.kind == K::Play) PlayClipFrom(go, c.name, m_clipWarned);
            else if (c.kind == K::Stop) StopClipFrom(go);
            else if (auto* sm = FindAnimComponent<AnimStateMachine>(go)) {
                if (c.kind == K::SetFloat) sm->SetFloat(c.name, c.value);
                else if (c.kind == K::SetBool) sm->SetBool(c.name, c.value != 0.0f);
                else if (c.kind == K::Trigger) sm->SetTrigger(c.name);
                else sm->GoTo(c.name);
            }
        } else {
            GameObject* go = d.audioTrack->object.empty() ? gameObject : (s ? Sequence::FindObject(*s, d.audioTrack->object) : nullptr);
            if (!go) continue;
            AudioSource* src = go->GetComponent<AudioSource>();
            if (!src) src = go->AddComponent<AudioSource>();   // a cue on an object without one still plays
            const SeqAudioCue& c = *d.audio;
            if (c.file.empty()) src->Play();
            else if (auto clip = LoadAudioFile(c.file)) src->PlayOneShot(*clip, c.volume);
            else if (!m_audioWarned) { m_audioWarned = true; Log::Warning("[sequence] cannot load sound '", c.file, "'"); }
        }
    }
    if (holdAt >= 0.0f) m_last = holdAt;
}

void SequencePlayer::Fire(const SeqEvent& e) {
    Scene* s = GetScene();
    using K = SeqEvent::Kind;
    switch (e.kind) {
    case K::Named:
        if (onEvent) onEvent(e.name);
        Deliver("on_sequence_event", "OnSequenceEvent", e.name);
        break;
    case K::SetActive:
        if (GameObject* go = s ? Sequence::FindObject(*s, e.name) : nullptr) go->active = e.value != 0.0f;
        break;
    case K::Teleport: {
        GameObject* go = s ? Sequence::FindObject(*s, e.name) : nullptr;
        GameObject* to = s ? Sequence::FindObject(*s, e.text) : nullptr;
        if (go && to && go->transform && to->transform) {
            go->transform->SetPosition(to->transform->Position());
            go->transform->SetRotation(to->transform->Rotation());
        }
        break;
    }
    case K::FadeOut: case K::FadeIn: case K::Subtitle:
        break;                                    // read back from the timeline (OverlayAt)
    case K::TimeScale:
        Time::SetTimeScale(std::max(0.0f, e.value));
        m_changedTimeScale = true;
        break;
    case K::WaitForInput:
        if (!m_skipping) m_waiting = true;
        break;
    case K::End:
        m_time = e.time;
        m_binding.Apply(m_seq, e.time);   // where the cutscene leaves everything
        Finish();
        break;
    }
}

// Scripts on every object get `scriptEvent(name)` (OkayScript, or the C#-style
// handler of the same name), visual scripts run their `vsEntry` nodes (named
// ones filter on the event), and Action Lists with an On Message trigger for
// the name fire. The same fan-out the inventory events use, scene-wide.
void SequencePlayer::Deliver(const char* scriptEvent, const char* vsEntry, const std::string& name) {
    Scene* s = GetScene();
    if (!s) return;
    s_lastEvent = name;
    const std::vector<vs::VsValue> args{vs::VsValue(name)};
    std::string key = name;
    for (char& c : key) if (c == ' ' || c == '\t') c = '_';   // entry names are one token
    const auto& objs = s->Objects();
    for (std::size_t i = 0; i < objs.size(); ++i) {         // by index: a handler may create objects
        GameObject* go = objs[i].get();
        if (!go || go->IsDestroyed() || !ScriptsRun(go)) continue;
        for (auto* sc : go->GetComponents<ScriptComponent>())
            if (sc->VM()) sc->VM()->CallEventArgs(scriptEvent, args);
        if (vsEntry)
            for (auto* vc : go->GetComponents<VisualScriptComponent>()) {
                vc->RaiseEvent((std::string(vsEntry) + ":" + key).c_str(), nullptr, name);
                vc->RaiseEvent(vsEntry, nullptr, name);
            }
        for (auto* al : go->GetComponents<ActionList>()) al->ReceiveMessage(name);
    }
    s_lastEvent.clear();
}

// ---- Lifecycle ------------------------------------------------------------------
void SequencePlayer::Start() {
    if (!playOnStart) return;
    if (ScriptsRun(gameObject)) Play();
    else m_startDeferred = true;   // the editor outside Play: start when scripts run
}

void SequencePlayer::Update(float dt) {
    if (!ScriptsRun(gameObject)) return;
    if (m_startDeferred) { m_startDeferred = false; Play(); }
    if (!m_playing) return;
    if (skipKey && Input::GetKeyDown(skipKey)) { Skip(); return; }
    Step(dt);
}

void SequencePlayer::LateUpdate(float) {
    if (!m_playing || !ScriptsRun(gameObject)) return;
    m_binding.Apply(m_seq, m_time);   // after the controllers placed the camera this frame
}

// ---- Scene-wide helpers ------------------------------------------------------------
SequencePlayer* SequencePlayer::Playing(const Scene& scene) {
    for (const auto& up : scene.Objects())
        if (up) if (auto* sp = up->GetComponent<SequencePlayer>()) if (sp->IsPlaying()) return sp;
    return nullptr;
}

bool SequencePlayer::AnyLocksInput(const Scene& scene) {
    for (const auto& up : scene.Objects())
        if (up) if (auto* sp = up->GetComponent<SequencePlayer>()) if (sp->LocksInput()) return true;
    return false;
}

void SequencePlayer::StopAll(Scene& scene) {
    for (const auto& up : scene.Objects())
        if (up) if (auto* sp = up->GetComponent<SequencePlayer>()) sp->Stop();
}

bool SequencePlayer::PlayByName(Scene& scene, GameObject* from, const std::string& name) {
    namespace fs = std::filesystem;
    if (name.empty()) return false;
    const std::vector<SequencePlayer*> players = scene.FindObjectsOfType<SequencePlayer>();
    for (SequencePlayer* sp : players)
        if (sp->sequencePath == name || fs::path(sp->sequencePath).stem().string() == name) return sp->Play();
    // A file by that name: as given, with the extension, or under Assets.
    std::string path;
    std::error_code ec;
    const std::string& base = Character::AssetBaseDir();
    for (const std::string& cand : {name, name + ".okayseq", "Assets/" + name + ".okayseq"}) {
        if (fs::exists(fs::path(cand), ec)) { path = cand; break; }
        if (!base.empty() && fs::exists(fs::path(base) / cand, ec)) { path = cand; break; }
    }
    if (path.empty()) { Log::Warning("[sequence] no sequence named '", name, "'"); return false; }
    SequencePlayer* sp = !players.empty() ? players.front() : (from ? from->AddComponent<SequencePlayer>() : nullptr);
    return sp && sp->Play(path);
}

// ---- Overlay ------------------------------------------------------------------------
std::vector<LoadingDrawCmd> SequencePlayer::BuildOverlay(const SeqOverlay& ov, float W, float H) {
    using K = LoadingDrawCmd::Kind;
    std::vector<LoadingDrawCmd> out;
    if (W <= 0.0f || H <= 0.0f) return out;
    if (ov.fade > 0.001f) {
        LoadingDrawCmd d; d.kind = K::Rect; d.x = 0; d.y = 0; d.w = W; d.h = H;
        d.color = Color::FromBytes(0, 0, 0); d.alpha = Mathf::Clamp01(ov.fade);
        out.push_back(d);
    }
    // The subtitle card: the launcher's slate panel with a hairline border, the
    // speaker in the accent colour above the line, low on the screen.
    if (!ov.text.empty() && ov.subtitle > 0.001f) {
        const float a = Mathf::Clamp01(ov.subtitle);
        const float th = Mathf::Clamp(H * 0.03f, 14.0f, 28.0f);        // text height
        const float sh = ov.speaker.empty() ? 0.0f : th * 0.85f;        // speaker line
        const float pad = th * 0.7f;
        const float chars = (float)std::max(ov.text.size(), ov.speaker.size());
        const float w = std::min(W * 0.8f, chars * th * 0.56f + pad * 2.0f);
        const float h = sh + th + pad * 2.0f + (sh > 0.0f ? th * 0.3f : 0.0f);
        const float x = (W - w) * 0.5f, y = H - h - H * 0.08f;
        LoadingDrawCmd panel; panel.kind = K::Rect; panel.x = x; panel.y = y; panel.w = w; panel.h = h;
        panel.color = Color::FromBytes(30, 33, 41); panel.radius = 8.0f; panel.alpha = 0.92f * a;
        out.push_back(panel);
        LoadingDrawCmd border = panel; border.kind = K::Outline; border.color = Color::White; border.alpha = 0.09f * a;
        out.push_back(border);
        float ty = y + pad;
        if (sh > 0.0f) {
            LoadingDrawCmd sp; sp.kind = K::Text; sp.text = ov.speaker; sp.x = W * 0.5f; sp.y = ty; sp.h = sh;
            sp.color = Color::FromBytes(66, 150, 250); sp.align = 1; sp.alpha = a;
            out.push_back(sp);
            ty += sh + th * 0.3f;
        }
        LoadingDrawCmd line; line.kind = K::Text; line.text = ov.text; line.x = W * 0.5f; line.y = ty; line.h = th;
        line.color = Color::FromBytes(230, 235, 245); line.align = 1; line.alpha = a;
        out.push_back(line);
    }
    if (ov.waiting) {
        const float th = Mathf::Clamp(H * 0.022f, 11.0f, 20.0f);
        LoadingDrawCmd hint; hint.kind = K::Text; hint.text = "Press Enter"; hint.x = W - th * 1.5f; hint.y = H - th * 2.5f;
        hint.h = th; hint.color = Color::FromBytes(140, 150, 171); hint.align = 2; hint.alpha = 1.0f;
        out.push_back(hint);
    }
    return out;
}

} // namespace okay
