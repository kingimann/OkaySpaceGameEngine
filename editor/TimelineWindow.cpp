// The Timeline window: the cutscene editor for .okayseq sequences. The track
// list sits on the left (camera, transform, animation, audio and event tracks,
// each bound to a scene object), their shots, keys and cues on a shared ruler in
// the middle (zoom with the wheel, pan with the middle button, drag to move,
// double-click to add), and the selection's fields on the right. Outside Play
// the playhead previews the sequence live: the tracked objects and the main
// camera follow it in the Scene and Game views, and everything goes back when
// the preview stops. Record keys whatever the gizmo or the Inspector changes.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "TimelineWindow.hpp"
#include "ConsolePanel.hpp"             // ConsoleLog
#include "EditorWidgets.hpp"            // AccentCol, SameLineRight
#include "MainShared.hpp"               // the Animation window's transport, ruler, playhead and key widgets
#include "okay/Components/Camera.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include "imgui.h"
#include "tinyfiledialogs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace okay;
using okay::editor::EditorState;

namespace oked {

bool g_showTimeline = false;

namespace {

constexpr float kRulerH = 22.0f, kRowH = 26.0f, kPad = 8.0f;
constexpr float kLeftW = 230.0f, kRightW = 310.0f;

enum class Kind { None = -1, Camera = 0, Transform, Animation, Audio, Events };

// The selection: an entry (a shot, a key time, a cue or an event) on a track.
// Transform keys select by time, so every channel keyed there moves together.
struct Sel { Kind kind = Kind::None; int track = -1; int index = -1; float time = 0.0f; };

// A tracked object's transform after the last preview apply (Record compares).
struct Snap { std::string path; Vec3 p; Quat r; Vec3 s; };

struct Row { Kind kind; int track; std::string label; };

struct State {
    Sequence seq;
    std::string path;              // the .okayseq file ("" = unsaved)
    bool dirty = false;
    std::vector<Sequence> undo, redo;
    unsigned version = 1;          // bumped by every edit: the preview re-applies
    float t = 0.0f;                // the playhead
    bool playing = false;          // edit-mode playback
    bool record = false;
    AnimTimeView view;
    int fps = 30;
    bool snap = true;
    Sel sel;
    Kind rowKind = Kind::None; int rowTrack = -1;   // the selected track row
    // The preview (edit mode): the tracked objects and the camera follow the playhead.
    SequenceBinding binding;
    std::uint64_t boundStructure = 0;
    std::string boundObjects;
    float lastApplied = -1.0f; unsigned appliedVersion = 0;
    std::vector<Snap> snaps;
    bool recGesture = false;       // Record pushed an undo step for the drag in progress
    // Dragging an entry, a loop marker or the playhead.
    bool dragging = false; float dragOrigin = 0.0f, dragStart = 0.0f; bool dragPushed = false;
    int loopDrag = 0;              // 1 = the start marker, 2 = the end marker
    bool scrubbing = false;
    std::string shotFrom;          // "Shot from object": the object picked
    SeqOverlay overlay; bool overlayValid = false;
};
State& S() { static State s; return s; }
bool g_focused = false;

// ---- Edits -------------------------------------------------------------------------
// Every change starts with BeginEdit (the undo step) and ends with Touch (the
// preview re-applies, the file is dirty).
void BeginEdit() {
    State& s = S();
    s.undo.push_back(s.seq);
    if (s.undo.size() > 100) s.undo.erase(s.undo.begin());
    s.redo.clear();
}
void Touch() { ++S().version; S().dirty = true; }

float Snapped(float t) {
    const State& s = S();
    t = std::max(0.0f, t);
    return s.snap ? Sequence::SnapToFrame(t, s.fps) : t;
}

const char* KindName(Kind k) {
    switch (k) {
    case Kind::Camera: return "Camera";
    case Kind::Transform: return "Transform";
    case Kind::Animation: return "Animation";
    case Kind::Audio: return "Audio";
    case Kind::Events: return "Events";
    default: return "";
    }
}

ImU32 KindColor(Kind k) {
    switch (k) {
    case Kind::Camera: return IM_COL32(90, 160, 240, 255);
    case Kind::Transform: return IM_COL32(120, 205, 130, 255);
    case Kind::Animation: return IM_COL32(240, 160, 80, 255);
    case Kind::Audio: return IM_COL32(230, 210, 90, 255);
    case Kind::Events: return IM_COL32(200, 130, 230, 255);
    default: return IM_COL32(200, 200, 200, 255);
    }
}

std::vector<Row> Rows(const Sequence& seq) {
    std::vector<Row> rows;
    if (seq.cameraTrack) rows.push_back({Kind::Camera, 0, "Camera"});
    for (int i = 0; i < (int)seq.transforms.size(); ++i) rows.push_back({Kind::Transform, i, seq.transforms[(std::size_t)i].object});
    for (int i = 0; i < (int)seq.animations.size(); ++i) rows.push_back({Kind::Animation, i, seq.animations[(std::size_t)i].object});
    for (int i = 0; i < (int)seq.audio.size(); ++i) rows.push_back({Kind::Audio, i, seq.audio[(std::size_t)i].object});
    for (int i = 0; i < (int)seq.events.size(); ++i) rows.push_back({Kind::Events, i, seq.events[(std::size_t)i].name});
    return rows;
}

// The key times of a transform track: the union of its channels' times.
std::vector<float> KeyTimes(const SeqTransformTrack& tr) {
    std::vector<float> out;
    for (const std::vector<SeqKey>* ch : {&tr.position, &tr.rotation, &tr.scale})
        for (const SeqKey& k : *ch) {
            bool have = false;
            for (float t : out) if (std::fabs(t - k.time) <= 1e-3f) { have = true; break; }
            if (!have) out.push_back(k.time);
        }
    std::sort(out.begin(), out.end());
    return out;
}

// Every entry time on every track (the transport's previous / next key).
std::vector<float> AllTimes(const Sequence& seq) {
    std::vector<float> out;
    for (const auto& sh : seq.shots) out.push_back(sh.time);
    for (const auto& tr : seq.transforms) for (float t : KeyTimes(tr)) out.push_back(t);
    for (const auto& tr : seq.animations) for (const auto& c : tr.cues) out.push_back(c.time);
    for (const auto& tr : seq.audio) for (const auto& c : tr.cues) out.push_back(c.time);
    for (const auto& tr : seq.events) for (const auto& e : tr.events) out.push_back(e.time);
    std::sort(out.begin(), out.end());
    return out;
}

// Move every channel key at `from` to `to`; a key already at `to` is replaced.
void MoveTransformKeys(SeqTransformTrack& tr, float from, float to) {
    for (std::vector<SeqKey>* ch : {&tr.position, &tr.rotation, &tr.scale}) {
        const int at = Sequence::KeyAt(*ch, from);
        if (at < 0) continue;
        SeqKey k = (*ch)[(std::size_t)at];
        ch->erase(ch->begin() + at);
        const int landing = Sequence::KeyAt(*ch, to);
        if (landing >= 0) ch->erase(ch->begin() + landing);
        k.time = to;
        std::size_t i = 0;
        while (i < ch->size() && (*ch)[i].time < to) ++i;
        ch->insert(ch->begin() + (std::ptrdiff_t)i, k);
    }
}

void EraseTransformKeys(SeqTransformTrack& tr, float at) {
    for (std::vector<SeqKey>* ch : {&tr.position, &tr.rotation, &tr.scale}) {
        const int i = Sequence::KeyAt(*ch, at);
        if (i >= 0) ch->erase(ch->begin() + i);
    }
}

// Retime one entry of a track (kept in time order; `index` follows it).
template <class T> void SetTimeOf(std::vector<T>& v, int& index, float t) {
    if (index < 0 || index >= (int)v.size()) return;
    T e = v[(std::size_t)index];
    e.time = t;
    v.erase(v.begin() + index);
    std::size_t i = 0;
    while (i < v.size() && v[i].time <= t) ++i;
    v.insert(v.begin() + (std::ptrdiff_t)i, e);
    index = (int)i;
}

bool SelValid(const State& s) {
    const Sel& e = s.sel;
    const Sequence& q = s.seq;
    switch (e.kind) {
    case Kind::Camera:    return q.cameraTrack && e.index >= 0 && e.index < (int)q.shots.size();
    case Kind::Transform: return e.track >= 0 && e.track < (int)q.transforms.size();
    case Kind::Animation: return e.track >= 0 && e.track < (int)q.animations.size() && e.index >= 0 && e.index < (int)q.animations[(std::size_t)e.track].cues.size();
    case Kind::Audio:     return e.track >= 0 && e.track < (int)q.audio.size() && e.index >= 0 && e.index < (int)q.audio[(std::size_t)e.track].cues.size();
    case Kind::Events:    return e.track >= 0 && e.track < (int)q.events.size() && e.index >= 0 && e.index < (int)q.events[(std::size_t)e.track].events.size();
    default: return false;
    }
}

float SelTime(const State& s) {
    if (!SelValid(s)) return 0.0f;
    const Sel& e = s.sel;
    switch (e.kind) {
    case Kind::Camera:    return s.seq.shots[(std::size_t)e.index].time;
    case Kind::Transform: return e.time;
    case Kind::Animation: return s.seq.animations[(std::size_t)e.track].cues[(std::size_t)e.index].time;
    case Kind::Audio:     return s.seq.audio[(std::size_t)e.track].cues[(std::size_t)e.index].time;
    case Kind::Events:    return s.seq.events[(std::size_t)e.track].events[(std::size_t)e.index].time;
    default: return 0.0f;
    }
}

void SetSelTime(State& s, float t) {
    if (!SelValid(s)) return;
    Sel& e = s.sel;
    switch (e.kind) {
    case Kind::Camera:    SetTimeOf(s.seq.shots, e.index, t); break;
    case Kind::Transform: MoveTransformKeys(s.seq.transforms[(std::size_t)e.track], e.time, t); e.time = t; break;
    case Kind::Animation: SetTimeOf(s.seq.animations[(std::size_t)e.track].cues, e.index, t); break;
    case Kind::Audio:     SetTimeOf(s.seq.audio[(std::size_t)e.track].cues, e.index, t); break;
    case Kind::Events:    SetTimeOf(s.seq.events[(std::size_t)e.track].events, e.index, t); break;
    default: break;
    }
}

void DeleteSel(State& s) {
    if (!SelValid(s)) return;
    BeginEdit();
    Sel& e = s.sel;
    switch (e.kind) {
    case Kind::Camera:    s.seq.shots.erase(s.seq.shots.begin() + e.index); break;
    case Kind::Transform: EraseTransformKeys(s.seq.transforms[(std::size_t)e.track], e.time); break;
    case Kind::Animation: { auto& v = s.seq.animations[(std::size_t)e.track].cues; v.erase(v.begin() + e.index); break; }
    case Kind::Audio:     { auto& v = s.seq.audio[(std::size_t)e.track].cues; v.erase(v.begin() + e.index); break; }
    case Kind::Events:    { auto& v = s.seq.events[(std::size_t)e.track].events; v.erase(v.begin() + e.index); break; }
    default: break;
    }
    e = Sel{};
    Touch();
}

void RemoveTrack(State& s, Kind k, int track) {
    BeginEdit();
    switch (k) {
    case Kind::Camera:    s.seq.cameraTrack = false; s.seq.shots.clear(); break;
    case Kind::Transform: s.seq.transforms.erase(s.seq.transforms.begin() + track); break;
    case Kind::Animation: s.seq.animations.erase(s.seq.animations.begin() + track); break;
    case Kind::Audio:     s.seq.audio.erase(s.seq.audio.begin() + track); break;
    case Kind::Events:    s.seq.events.erase(s.seq.events.begin() + track); break;
    default: break;
    }
    s.sel = Sel{}; s.rowKind = Kind::None; s.rowTrack = -1;
    Touch();
}

// A transform key for `tr`'s object as it stands in the scene, every channel.
void KeyObjectNow(EditorState& ed, SeqTransformTrack& tr, float t) {
    GameObject* go = Sequence::FindObject(ed.scene(), tr.object);
    if (!go || !go->transform) return;
    Sequence::SetKey(tr.position, t, go->transform->localPosition);
    Sequence::SetKey(tr.rotation, t, go->transform->localRotation.ToEuler());
    Sequence::SetKey(tr.scale, t, go->transform->localScale);
}

// The Key button: the selected transform track's object, or every tracked object.
void KeyNow(EditorState& ed) {
    State& s = S();
    if (s.seq.transforms.empty()) return;
    BeginEdit();
    if (s.rowKind == Kind::Transform && s.rowTrack >= 0 && s.rowTrack < (int)s.seq.transforms.size()) {
        KeyObjectNow(ed, s.seq.transforms[(std::size_t)s.rowTrack], s.t);
        s.sel = {Kind::Transform, s.rowTrack, -1, s.t};
    } else {
        for (auto& tr : s.seq.transforms) KeyObjectNow(ed, tr, s.t);
    }
    Touch();
}

void AddTrack(EditorState& ed, Kind k, bool subtitles) {
    State& s = S();
    BeginEdit();
    const std::string obj = ed.selected() ? ed.selected()->name : std::string();
    switch (k) {
    case Kind::Camera:    s.seq.cameraTrack = true; s.rowTrack = 0; break;
    case Kind::Transform: s.seq.transforms.push_back({obj, {}, {}, {}}); s.rowTrack = (int)s.seq.transforms.size() - 1; break;
    case Kind::Animation: s.seq.animations.push_back({obj, {}}); s.rowTrack = (int)s.seq.animations.size() - 1; break;
    case Kind::Audio:     s.seq.audio.push_back({obj, {}}); s.rowTrack = (int)s.seq.audio.size() - 1; break;
    case Kind::Events: {
        SeqEventTrack tr; tr.name = subtitles ? "Subtitles" : "Events";
        s.seq.events.push_back(tr); s.rowTrack = (int)s.seq.events.size() - 1;
        break;
    }
    default: break;
    }
    s.rowKind = k;
    s.sel = Sel{};
    Touch();
}

// A new entry at `t` on a track: a shot of what the Game view shows now, a key of
// the object as it stands, an empty cue, a named event (a subtitle on a
// Subtitles track).
void AddEntry(EditorState& ed, Kind k, int track, float t) {
    State& s = S();
    BeginEdit();
    switch (k) {
    case Kind::Camera: {
        SeqShot sh; sh.time = t;
        if (Camera* cam = s.binding.camera(); cam && cam->transform) {
            sh.position = cam->transform->Position();
            sh.rotation = cam->transform->Rotation().ToEuler();
            sh.fov = cam->fieldOfView;
        }
        std::size_t i = 0;
        while (i < s.seq.shots.size() && s.seq.shots[i].time <= t) ++i;
        s.seq.shots.insert(s.seq.shots.begin() + (std::ptrdiff_t)i, sh);
        s.sel = {Kind::Camera, 0, (int)i, t};
        break;
    }
    case Kind::Transform:
        KeyObjectNow(ed, s.seq.transforms[(std::size_t)track], t);
        s.sel = {Kind::Transform, track, -1, t};
        break;
    case Kind::Animation: {
        auto& v = s.seq.animations[(std::size_t)track].cues;
        SeqAnimCue c; c.time = t;
        std::size_t i = 0; while (i < v.size() && v[i].time <= t) ++i;
        v.insert(v.begin() + (std::ptrdiff_t)i, c);
        s.sel = {Kind::Animation, track, (int)i, t};
        break;
    }
    case Kind::Audio: {
        auto& v = s.seq.audio[(std::size_t)track].cues;
        SeqAudioCue c; c.time = t;
        std::size_t i = 0; while (i < v.size() && v[i].time <= t) ++i;
        v.insert(v.begin() + (std::ptrdiff_t)i, c);
        s.sel = {Kind::Audio, track, (int)i, t};
        break;
    }
    case Kind::Events: {
        auto& tr = s.seq.events[(std::size_t)track];
        SeqEvent e; e.time = t;
        if (tr.name == "Subtitles") { e.kind = SeqEvent::Kind::Subtitle; e.text = "..."; e.value = 2.0f; }
        else e.name = "event";
        std::size_t i = 0; while (i < tr.events.size() && tr.events[i].time <= t) ++i;
        tr.events.insert(tr.events.begin() + (std::ptrdiff_t)i, e);
        s.sel = {Kind::Events, track, (int)i, t};
        break;
    }
    default: break;
    }
    s.rowKind = k; s.rowTrack = track;
    Touch();
}

// ---- The preview ------------------------------------------------------------------
std::string ObjectsKey(const Sequence& seq) {
    std::string k;
    for (const auto& tr : seq.transforms) { k += tr.object; k += '\n'; }
    return k;
}

void TakeSnaps(Scene& scene) {
    State& s = S();
    s.snaps.clear();
    for (const auto& tr : s.seq.transforms)
        if (GameObject* go = Sequence::FindObject(scene, tr.object); go && go->transform)
            s.snaps.push_back({tr.object, go->transform->localPosition, go->transform->localRotation, go->transform->localScale});
}

void ReleasePreview() {
    State& s = S();
    if (s.binding.Bound()) { s.binding.RestoreAll(); s.binding.Unbind(); }
    s.snaps.clear();
    s.lastApplied = -1.0f;
    s.overlayValid = false;
    s.recGesture = false;
}

void PreviewTick(EditorState& ed) {
    State& s = S();
    Scene& scene = ed.scene();
    if (ed.isPlaying()) { if (s.binding.Bound()) ReleasePreview(); return; }
    const std::string key = ObjectsKey(s.seq);
    if (s.binding.Bound() && (s.binding.scene() != &scene || s.boundStructure != scene.StructureVersion() || s.boundObjects != key))
        ReleasePreview();   // the objects changed under it: start over from what is there now
    if (!s.binding.Bound()) {
        s.binding.Bind(scene, s.seq);
        s.boundStructure = scene.StructureVersion();
        s.boundObjects = key;
    }
    // Record: whatever moved since the last apply (the gizmo, the Inspector) is
    // keyed at the playhead; one undo step per drag.
    if (s.record && !s.playing && !s.snaps.empty()) {
        for (const Snap& sn : s.snaps) {
            GameObject* go = Sequence::FindObject(scene, sn.path);
            if (!go || !go->transform) continue;
            Transform* tr = go->transform;
            const bool movedP = (tr->localPosition - sn.p).Magnitude() > 1e-5f;
            const Quat a = tr->localRotation, b = sn.r;
            const bool movedR = std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z) + std::fabs(a.w - b.w) > 1e-5f;
            const bool movedS = (tr->localScale - sn.s).Magnitude() > 1e-5f;
            if (!movedP && !movedR && !movedS) continue;
            if (!s.recGesture) { BeginEdit(); s.recGesture = true; }
            for (auto& track : s.seq.transforms) {
                if (track.object != sn.path) continue;
                if (movedP) Sequence::SetKey(track.position, s.t, tr->localPosition);
                if (movedR) Sequence::SetKey(track.rotation, s.t, tr->localRotation.ToEuler());
                if (movedS) Sequence::SetKey(track.scale, s.t, tr->localScale);
                s.sel = {Kind::Transform, (int)(&track - &s.seq.transforms[0]), -1, s.t};
            }
            Touch();
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s.recGesture = false;
    if (s.lastApplied != s.t || s.appliedVersion != s.version || s.snaps.empty()) {
        s.binding.Apply(s.seq, s.t);
        s.lastApplied = s.t; s.appliedVersion = s.version;
        TakeSnaps(scene);
    }
    s.overlay = s.seq.OverlayAt(s.t);
    s.overlayValid = true;
}

// ---- Files ----------------------------------------------------------------------------
std::string ProjectRelative(const EditorState& ed, const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (ed.projectDir().empty()) return path;
    const fs::path rel = fs::relative(fs::path(path), fs::path(ed.projectDir()), ec);
    if (ec || rel.empty() || rel.string().rfind("..", 0) == 0) return path;
    return rel.generic_string();
}

bool DoOpen(const std::string& path) {
    Sequence seq;
    std::string err;
    if (!Sequence::LoadFromFile(path, seq, &err)) { ConsoleLog("Timeline: " + err, 2); return false; }
    State& s = S();
    ReleasePreview();
    s.seq = seq; s.path = path; s.dirty = false;
    s.undo.clear(); s.redo.clear(); ++s.version;
    s.t = 0.0f; s.playing = false; s.sel = Sel{}; s.rowKind = Kind::None; s.rowTrack = -1;
    s.view = AnimTimeView{};
    g_showTimeline = true;
    return true;
}

void DoNew() {
    State& s = S();
    ReleasePreview();
    s.seq = Sequence{}; s.path.clear(); s.dirty = false;
    s.undo.clear(); s.redo.clear(); ++s.version;
    s.t = 0.0f; s.playing = false; s.sel = Sel{}; s.rowKind = Kind::None; s.rowTrack = -1;
    s.view = AnimTimeView{};
}

bool DoSave(EditorState& ed, bool saveAs) {
    namespace fs = std::filesystem;
    State& s = S();
    std::string path = s.path;
    if (saveAs || path.empty()) {
        const char* filt[] = {"*.okayseq"};
        const fs::path dir = fs::path(ed.projectDir().empty() ? "." : ed.projectDir()) / "Assets";
        const std::string def = (dir / (s.seq.name + ".okayseq")).string();
        const char* p = tinyfd_saveFileDialog("Save sequence", def.c_str(), 1, filt, "OkaySpace sequence");
        if (!p) return false;
        path = p;
        if (fs::path(path).extension() != ".okayseq") path += ".okayseq";
    }
    std::string err;
    if (!s.seq.SaveToFile(path, &err)) { ConsoleLog("Timeline: " + err, 2); return false; }
    s.path = path; s.dirty = false;
    ConsoleLog("Saved " + path);
    // Players using this file play the new version.
    std::error_code ec;
    for (SequencePlayer* sp : ed.scene().FindObjectsOfType<SequencePlayer>())
        if (fs::equivalent(fs::path(sp->ResolvedPath()), fs::path(path), ec)) sp->Load();
    return true;
}

// Play mode: run this sequence through a Sequence Player in the scene (one using
// this file, else the first, else a new object), with the edits made here.
void PlayInGame(EditorState& ed) {
    namespace fs = std::filesystem;
    State& s = S();
    SequencePlayer* sp = nullptr;
    std::error_code ec;
    auto players = ed.scene().FindObjectsOfType<SequencePlayer>();
    for (SequencePlayer* p : players)
        if (!s.path.empty() && fs::equivalent(fs::path(p->ResolvedPath()), fs::path(s.path), ec)) { sp = p; break; }
    if (!sp && !players.empty()) sp = players.front();
    if (!sp) sp = ed.scene().CreateGameObject("Sequence Player")->AddComponent<SequencePlayer>();
    if (!s.path.empty()) sp->sequencePath = ProjectRelative(ed, s.path);
    sp->SetSequence(s.seq);
    sp->Play();
}

// ---- Widgets ------------------------------------------------------------------------------
// Field wrappers: one undo step when a field is first touched, the preview
// re-applies on every change.
bool Drag(const char* label, float& v, float speed, float lo, float hi, const char* fmt = "%.2f") {
    const bool changed = ImGui::DragFloat(label, &v, speed, lo, hi, fmt);
    if (ImGui::IsItemActivated()) BeginEdit();
    if (changed) Touch();
    return changed;
}
bool Drag3(const char* label, Vec3& v, float speed) {
    float a[3] = {v.x, v.y, v.z};
    const bool changed = ImGui::DragFloat3(label, a, speed);
    if (ImGui::IsItemActivated()) BeginEdit();
    if (changed) { v = {a[0], a[1], a[2]}; Touch(); }
    return changed;
}
bool Check(const char* label, bool& v) {
    bool x = v;
    if (!ImGui::Checkbox(label, &x)) return false;
    BeginEdit(); v = x; Touch();
    return true;
}
bool TextField(const char* label, std::string& v, const char* hint = nullptr) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", v.c_str());
    const bool changed = hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof(buf)) : ImGui::InputText(label, buf, sizeof(buf));
    if (ImGui::IsItemActivated()) BeginEdit();
    if (changed) { v = buf; Touch(); }
    return changed;
}
bool EaseCombo(const char* label, Ease& e) {
    bool changed = false;
    if (ImGui::BeginCombo(label, Easing::Name(e))) {
        for (int i = 0; i < (int)Ease::Count; ++i) {
            const bool sel = (int)e == i;
            if (ImGui::Selectable(Easing::Name((Ease)i), sel)) { BeginEdit(); e = (Ease)i; Touch(); changed = true; }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}
// A scene object by name; `cameras` lists only objects with a Camera plus "(inline pose)".
bool ObjectCombo(const char* label, std::string& name, EditorState& ed, bool cameras) {
    bool changed = false;
    if (ImGui::BeginCombo(label, name.empty() ? (cameras ? "(inline pose)" : "(none)") : name.c_str())) {
        if (cameras && ImGui::Selectable("(inline pose)", name.empty())) { BeginEdit(); name.clear(); Touch(); changed = true; }
        int i = 0;
        for (const auto& up : ed.scene().Objects()) {
            if (!up || up->transient) continue;
            if (cameras && !up->GetComponent<Camera>()) continue;
            ImGui::PushID(i++);
            if (ImGui::Selectable(up->name.c_str(), up->name == name)) { BeginEdit(); name = up->name; Touch(); changed = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}
template <class E> bool KindCombo(const char* label, E& value, const char* const* names, int count) {
    bool changed = false;
    if (ImGui::BeginCombo(label, names[(int)value])) {
        for (int i = 0; i < count; ++i)
            if (ImGui::Selectable(names[i], (int)value == i)) { BeginEdit(); value = (E)i; Touch(); changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

// The time field every entry starts with.
void TimeField(State& s) {
    float t = SelTime(s);
    if (ImGui::DragFloat("Time##sel", &t, 0.01f, 0.0f, 3600.0f, "%.2f s")) { if (!s.dragPushed) { BeginEdit(); s.dragPushed = true; } SetSelTime(s, Snapped(t)); Touch(); }
    if (ImGui::IsItemDeactivated()) s.dragPushed = false;
}

// ---- The track list (left) --------------------------------------------------------------
void DrawTracks(EditorState& ed, const std::vector<Row>& rows) {
    State& s = S();
    ImGui::TextDisabled("Tracks");
    ImGui::Separator();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        ImGui::PushID((int)i);
        const bool selected = s.rowKind == r.kind && s.rowTrack == r.track;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(ImVec2(p.x, p.y + 4.0f), ImVec2(p.x + 4.0f, p.y + kRowH - 4.0f), KindColor(r.kind), 2.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        std::string label = r.kind == Kind::Camera || r.kind == Kind::Events ? r.label
                          : std::string(KindName(r.kind)) + ": " + (r.label.empty() ? "(no object)" : r.label);
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowOverlap, ImVec2(kLeftW - 70.0f, kRowH - 2.0f))) {
            s.rowKind = r.kind; s.rowTrack = r.track; s.sel = Sel{};
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.kind == Kind::Camera ? "The main camera's shots" : r.kind == Kind::Events ? "Named events and built-in actions" : "Select to edit the object; + adds an entry at the playhead");
        ImGui::SameLine(kLeftW - 56.0f);
        if (ImGui::SmallButton("+")) AddEntry(ed, r.kind, r.track, Snapped(s.t));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add at the playhead (or double-click the row)");
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) RemoveTrack(s, r.kind, r.track);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this track");
        ImGui::PopID();
    }
    if (rows.empty()) ImGui::TextDisabled("No tracks yet.\nAdd one below, then double-click\nits row to place entries.");
    ImGui::Spacing();
    if (ImGui::Button("+ Add Track", ImVec2(kLeftW - 16.0f, 0))) ImGui::OpenPopup("##tladd");
    if (ImGui::BeginPopup("##tladd")) {
        if (!s.seq.cameraTrack && ImGui::MenuItem("Camera")) AddTrack(ed, Kind::Camera, false);
        if (ImGui::MenuItem("Transform (object)")) AddTrack(ed, Kind::Transform, false);
        if (ImGui::MenuItem("Animation (object)")) AddTrack(ed, Kind::Animation, false);
        if (ImGui::MenuItem("Audio (object)")) AddTrack(ed, Kind::Audio, false);
        if (ImGui::MenuItem("Events")) AddTrack(ed, Kind::Events, false);
        if (ImGui::MenuItem("Subtitles")) AddTrack(ed, Kind::Events, true);
        ImGui::EndPopup();
    }
}

// ---- The grid (middle) ---------------------------------------------------------------------
void DrawGrid(EditorState& ed, const std::vector<Row>& rows) {
    State& s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float W = std::max(120.0f, avail.x), H = std::max(kRulerH + kRowH, avail.y);
    const float dur = s.seq.Duration();
    if (s.view.len <= 0.0f) AnimTimeViewFit(s.view, std::max(dur, 8.0f));
    auto tToX = [&](float t) { return o.x + kPad + ((t - s.view.start) / s.view.len) * (W - kPad * 2.0f); };
    auto xToT = [&](float x) { return s.view.start + (x - o.x - kPad) / (W - kPad * 2.0f) * s.view.len; };
    const float pxPerSec = (W - kPad * 2.0f) / s.view.len;
    const float rowsTop = o.y + kRulerH;
    const float bottom = std::max(rowsTop + (float)rows.size() * kRowH, o.y + H);

    ImGui::InvisibleButton("##tlgrid", ImVec2(W, H));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float mouseT = xToT(mouse.x);
    if (hovered) AnimTimeViewInput(s.view, pxPerSec, mouseT);

    dl->PushClipRect(o, ImVec2(o.x + W, o.y + H), true);
    dl->AddRectFilled(o, ImVec2(o.x + W, o.y + H), ImGui::GetColorU32(ImGuiCol_WindowBg));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const float y = rowsTop + (float)i * kRowH;
        const bool rowSel = s.rowKind == rows[i].kind && s.rowTrack == rows[i].track;
        dl->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + W, y + kRowH), rowSel ? ImGui::GetColorU32(AccentCol(0.10f)) : (i % 2 ? IM_COL32(255, 255, 255, 7) : 0));
        dl->AddLine(ImVec2(o.x, y + kRowH), ImVec2(o.x + W, y + kRowH), ImGui::GetColorU32(ImGuiCol_Separator, 0.5f));
    }
    AnimDrawRuler(dl, o, W, kRulerH, bottom, s.view, kPad, dur, s.fps);
    // The loop range and its markers on the ruler.
    const float loopEnd = s.seq.LoopEndTime();
    float lx0 = tToX(s.seq.loopStart), lx1 = tToX(loopEnd);
    if (s.seq.loop) {
        dl->AddRectFilled(ImVec2(lx0, rowsTop), ImVec2(lx1, bottom), IM_COL32(90, 160, 240, 20));
        const ImU32 mc = IM_COL32(90, 160, 240, 255);
        dl->AddTriangleFilled(ImVec2(lx0, o.y + kRulerH), ImVec2(lx0, o.y + kRulerH - 9.0f), ImVec2(lx0 + 8.0f, o.y + kRulerH), mc);
        dl->AddTriangleFilled(ImVec2(lx1, o.y + kRulerH), ImVec2(lx1, o.y + kRulerH - 9.0f), ImVec2(lx1 - 8.0f, o.y + kRulerH), mc);
    }
    // Entries, finding the one under the mouse on the way.
    Sel hot;
    const float headX = tToX(s.t);
    auto nearDiamond = [&](ImVec2 c) { return hovered && std::fabs(mouse.x - c.x) <= 8.0f && std::fabs(mouse.y - c.y) <= 9.0f; };
    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text, 0.85f);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        const float y = rowsTop + (float)i * kRowH, cy = y + kRowH * 0.5f;
        const ImU32 col = KindColor(r.kind);
        if (r.kind == Kind::Camera) {
            for (std::size_t j = 0; j < s.seq.shots.size(); ++j) {
                const SeqShot& sh = s.seq.shots[j];
                const float end = j + 1 < s.seq.shots.size() ? s.seq.shots[j + 1].time : std::max(dur, sh.time + 0.5f);
                const float x0 = tToX(sh.time), x1 = std::max(tToX(end) - 1.0f, x0 + 6.0f);
                const bool selected = s.sel.kind == Kind::Camera && s.sel.index == (int)j;
                const bool over = hovered && mouse.x >= x0 && mouse.x <= x1 && mouse.y >= y && mouse.y <= y + kRowH;
                if (over) hot = {Kind::Camera, 0, (int)j, sh.time};
                dl->AddRectFilled(ImVec2(x0, y + 3.0f), ImVec2(x1, y + kRowH - 3.0f), (col & 0x00FFFFFF) | ((over || selected ? 170u : 110u) << 24), 3.0f);
                if (sh.blend > 0.0f) {   // the ramp in from the previous shot
                    const float xb = std::min(tToX(sh.time + sh.blend), x1);
                    dl->AddTriangleFilled(ImVec2(x0, y + kRowH - 3.0f), ImVec2(xb, y + 3.0f), ImVec2(xb, y + kRowH - 3.0f), IM_COL32(255, 255, 255, 60));
                }
                if (selected) dl->AddRect(ImVec2(x0, y + 3.0f), ImVec2(x1, y + kRowH - 3.0f), IM_COL32(255, 255, 255, 230), 3.0f, 0, 1.5f);
                dl->PushClipRect(ImVec2(x0, y), ImVec2(x1, y + kRowH), true);
                dl->AddText(ImVec2(x0 + 5.0f, cy - ImGui::GetTextLineHeight() * 0.5f), IM_COL32(255, 255, 255, 230), sh.camera.empty() ? "Pose" : sh.camera.c_str());
                dl->PopClipRect();
            }
            continue;
        }
        // A diamond with its label, which stops short of the next entry (`nextT`) on the row.
        auto diamond = [&](float t, float nextT, const std::string& label, bool selected, const Sel& id) {
            const ImVec2 c(tToX(t), cy);
            const bool over = nearDiamond(c);
            if (over) hot = id;
            AnimDrawDiamond(dl, c, 6.0f, col, true, over, selected, std::fabs(t - s.t) < 1e-3f);
            if (label.empty()) return;
            const float xEnd = nextT > t ? tToX(nextT) - 8.0f : o.x + W;
            if (xEnd <= c.x + 12.0f) return;
            dl->PushClipRect(ImVec2(c.x + 9.0f, y), ImVec2(xEnd, y + kRowH), true);
            dl->AddText(ImVec2(c.x + 9.0f, cy - ImGui::GetTextLineHeight() * 0.5f), textCol, label.c_str());
            dl->PopClipRect();
        };
        if (r.kind == Kind::Transform) {
            for (float t : KeyTimes(s.seq.transforms[(std::size_t)r.track]))
                diamond(t, 0.0f, "", s.sel.kind == Kind::Transform && s.sel.track == r.track && std::fabs(s.sel.time - t) < 1e-3f, {Kind::Transform, r.track, -1, t});
        } else if (r.kind == Kind::Animation) {
            const auto& cues = s.seq.animations[(std::size_t)r.track].cues;
            for (std::size_t j = 0; j < cues.size(); ++j) {
                using K = SeqAnimCue::Kind;
                const SeqAnimCue& c = cues[j];
                const std::string label = c.kind == K::Play ? c.name : c.kind == K::Stop ? "stop" : c.kind == K::Trigger ? c.name + "!"
                                        : c.kind == K::GoToState ? "-> " + c.name : c.name + " = " + (c.kind == K::SetBool ? (c.value != 0.0f ? "true" : "false") : std::to_string(c.value).substr(0, 4));
                diamond(c.time, j + 1 < cues.size() ? cues[j + 1].time : 0.0f, label, s.sel.kind == Kind::Animation && s.sel.track == r.track && s.sel.index == (int)j, {Kind::Animation, r.track, (int)j, c.time});
            }
        } else if (r.kind == Kind::Audio) {
            const auto& cues = s.seq.audio[(std::size_t)r.track].cues;
            for (std::size_t j = 0; j < cues.size(); ++j)
                diamond(cues[j].time, j + 1 < cues.size() ? cues[j + 1].time : 0.0f, cues[j].file.empty() ? "clip" : std::filesystem::path(cues[j].file).stem().string(),
                        s.sel.kind == Kind::Audio && s.sel.track == r.track && s.sel.index == (int)j, {Kind::Audio, r.track, (int)j, cues[j].time});
        } else if (r.kind == Kind::Events) {
            const auto& evs = s.seq.events[(std::size_t)r.track].events;
            for (std::size_t j = 0; j < evs.size(); ++j) {
                using K = SeqEvent::Kind;
                const SeqEvent& e = evs[j];
                const std::string label = e.kind == K::Named ? e.name : e.kind == K::SetActive ? e.name + (e.value != 0.0f ? " on" : " off")
                                        : e.kind == K::Teleport ? e.name + " -> " + e.text : e.kind == K::FadeOut ? "fade out" : e.kind == K::FadeIn ? "fade in"
                                        : e.kind == K::Subtitle ? "\"" + e.text + "\"" : e.kind == K::TimeScale ? "time x" + std::to_string(e.value).substr(0, 4)
                                        : e.kind == K::WaitForInput ? "wait for input" : "end";
                diamond(e.time, j + 1 < evs.size() ? evs[j + 1].time : 0.0f, label, s.sel.kind == Kind::Events && s.sel.track == r.track && s.sel.index == (int)j, {Kind::Events, r.track, (int)j, e.time});
            }
        }
    }
    if (headX >= o.x && headX <= o.x + W) AnimDrawPlayhead(dl, headX, o.y, bottom, s.record);
    dl->PopClipRect();

    // ---- Interaction ----
    const bool onRuler = hovered && mouse.y < rowsTop;
    const int rowAt = (mouse.y >= rowsTop && (int)((mouse.y - rowsTop) / kRowH) < (int)rows.size()) ? (int)((mouse.y - rowsTop) / kRowH) : -1;
    static int ctxRow = -1; static float ctxT = 0.0f;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hot.kind != Kind::None) {
            s.sel = hot; s.rowKind = hot.kind; s.rowTrack = hot.track;
            s.dragging = true; s.dragOrigin = SelTime(s); s.dragStart = mouseT; s.dragPushed = false;
        } else if (onRuler && s.seq.loop && std::fabs(mouse.x - lx0) <= 9.0f) { BeginEdit(); s.loopDrag = 1; }
        else if (onRuler && s.seq.loop && std::fabs(mouse.x - lx1) <= 9.0f) { BeginEdit(); s.loopDrag = 2; }
        else {
            s.scrubbing = true; s.playing = false; s.t = Snapped(mouseT);
            s.sel = Sel{};
            if (rowAt >= 0) { s.rowKind = rows[(std::size_t)rowAt].kind; s.rowTrack = rows[(std::size_t)rowAt].track; }
        }
    }
    if (s.dragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float nt = Snapped(s.dragOrigin + (mouseT - s.dragStart));
            if (std::fabs(nt - SelTime(s)) > 1e-6f) {
                if (!s.dragPushed) { BeginEdit(); s.dragPushed = true; }
                SetSelTime(s, nt); Touch();
            }
        } else s.dragging = false;
    }
    if (s.scrubbing) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) s.t = Snapped(xToT(mouse.x));
        else s.scrubbing = false;
    }
    if (s.loopDrag) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float nt = Snapped(mouseT);
            if (s.loopDrag == 1) s.seq.loopStart = std::min(nt, std::max(0.0f, loopEnd - 0.05f));
            else s.seq.loopEnd = std::max(nt, s.seq.loopStart + 0.05f);
            Touch();
        } else s.loopDrag = 0;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hot.kind == Kind::None && rowAt >= 0)
        AddEntry(ed, rows[(std::size_t)rowAt].kind, rows[(std::size_t)rowAt].track, Snapped(mouseT));
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ctxRow = rowAt; ctxT = Snapped(mouseT);
        if (hot.kind != Kind::None) { s.sel = hot; s.rowKind = hot.kind; s.rowTrack = hot.track; }
        ImGui::OpenPopup("##tlctx");
    }
    if (ImGui::BeginPopup("##tlctx")) {
        if (ctxRow >= 0 && ctxRow < (int)rows.size()) {
            char lb[64]; std::snprintf(lb, sizeof(lb), "Add here (%.2f s)", ctxT);
            if (ImGui::MenuItem(lb)) AddEntry(ed, rows[(std::size_t)ctxRow].kind, rows[(std::size_t)ctxRow].track, ctxT);
        }
        if (SelValid(s) && ImGui::MenuItem("Delete entry", "Del")) DeleteSel(s);
        if (ctxRow >= 0 && ctxRow < (int)rows.size() && ImGui::MenuItem("Remove track")) RemoveTrack(s, rows[(std::size_t)ctxRow].kind, rows[(std::size_t)ctxRow].track);
        ImGui::EndPopup();
    }
    if (hovered && onRuler && ImGui::IsMouseHoveringRect(o, ImVec2(o.x + W, rowsTop)) && !s.scrubbing && !s.loopDrag)
        ImGui::SetTooltip("Click or drag to scrub. Wheel zooms, middle-drag pans.%s", s.seq.loop ? "\nDrag the blue markers to set the loop range." : "");
}

// ---- The inspector (right) ------------------------------------------------------------------
void ShotInspector(EditorState& ed, State& s) {
    SeqShot& sh = s.seq.shots[(std::size_t)s.sel.index];
    ImGui::TextColored(AccentCol(1.0f), "Shot %d", s.sel.index + 1);
    TimeField(s);
    ObjectCombo("Camera##shot", sh.camera, ed, true);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A camera object in the scene (the shot follows it), or an inline pose.");
    if (sh.camera.empty()) {
        Drag3("Position##shot", sh.position, 0.05f);
        Drag3("Rotation##shot", sh.rotation, 0.5f);
        Drag("Field of View##shot", sh.fov, 0.5f, 0.0f, 170.0f, "%.1f (0 = keep)");
        if (ImGui::Button("Shot from Scene view")) {
            // The editor camera's eye and look direction (the Scene view's Align camera to view).
            const float yawR = ed.camYaw * Mathf::Deg2Rad, pitchR = ed.camPitch * Mathf::Deg2Rad;
            const Vec3 dir{Mathf::Cos(pitchR) * Mathf::Sin(yawR), Mathf::Sin(pitchR), Mathf::Cos(pitchR) * Mathf::Cos(yawR)};
            const Vec3 eye = ed.camTarget + dir * ed.camDist;
            BeginEdit();
            sh.position = eye;
            // A Camera looks down its local -Z: its +Z points away from the target.
            sh.rotation = Quat::LookRotation((eye - ed.camTarget).Normalized(), Vec3::Up).ToEuler();
            sh.fov = g_editorFov;
            Touch();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Take the Scene view's own camera as this shot (3D view).");
        ObjectCombo("From object##shot", s.shotFrom, ed, false);
        ImGui::SameLine();
        if (ImGui::Button("Take##shotfrom")) {
            if (GameObject* go = Sequence::FindObject(ed.scene(), s.shotFrom); go && go->transform) {
                BeginEdit();
                sh.position = go->transform->Position();
                sh.rotation = go->transform->Rotation().ToEuler();
                if (auto* cam = go->GetComponent<Camera>()) sh.fov = cam->fieldOfView;
                Touch();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy that object's pose into this shot.");
    }
    ImGui::Separator();
    bool cut = sh.blend <= 0.0f;
    if (ImGui::Checkbox("Cut##shot", &cut)) { BeginEdit(); sh.blend = cut ? 0.0f : 1.0f; Touch(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jump to this shot. Off: ease in from the previous shot over the blend time.");
    if (!cut) {
        Drag("Blend##shot", sh.blend, 0.01f, 0.01f, 30.0f, "%.2f s");
        EaseCombo("Easing##shot", sh.ease);
    }
    if (ImGui::Button("Delete shot")) DeleteSel(s);
}

void TransformKeyInspector(State& s) {
    SeqTransformTrack& tr = s.seq.transforms[(std::size_t)s.sel.track];
    ImGui::TextColored(AccentCol(1.0f), "Key at %.2f s", s.sel.time);
    ImGui::TextDisabled("%s", tr.object.empty() ? "(no object)" : tr.object.c_str());
    TimeField(s);
    auto channel = [&](const char* name, std::vector<SeqKey>& keys, float speed, const Vec3& def) {
        ImGui::PushID(name);
        int i = Sequence::KeyAt(keys, s.sel.time);
        bool keyed = i >= 0;
        if (ImGui::Checkbox("##keyed", &keyed)) {
            BeginEdit();
            if (keyed) {
                Vec3 v = def;
                Sequence::ChannelAt(keys, s.sel.time, v);   // the value the channel has here now
                i = Sequence::SetKey(keys, s.sel.time, v);
            } else { keys.erase(keys.begin() + i); i = -1; }
            Touch();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keyed here");
        ImGui::SameLine();
        if (i >= 0) {
            Drag3(name, keys[(std::size_t)i].value, speed);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::SetNextItemWidth(110.0f);
            EaseCombo("##ease", keys[(std::size_t)i].ease);
        } else ImGui::TextDisabled("%s: not keyed", name);
        ImGui::PopID();
    };
    ImGui::PushItemWidth(150.0f);
    channel("Position", tr.position, 0.05f, Vec3{0, 0, 0});
    channel("Rotation", tr.rotation, 0.5f, Vec3{0, 0, 0});
    channel("Scale", tr.scale, 0.01f, Vec3{1, 1, 1});
    ImGui::PopItemWidth();
    ImGui::TextDisabled("Easing shapes the span from this key to the next.");
    if (ImGui::Button("Delete key")) DeleteSel(s);
}

void AnimCueInspector(State& s) {
    SeqAnimCue& c = s.seq.animations[(std::size_t)s.sel.track].cues[(std::size_t)s.sel.index];
    ImGui::TextColored(AccentCol(1.0f), "Animation cue");
    TimeField(s);
    static const char* kKinds[] = {"Play clip", "Stop clip", "Set float", "Set bool", "Trigger", "Go to state"};
    KindCombo("Action##anim", c.kind, kKinds, 6);
    using K = SeqAnimCue::Kind;
    if (c.kind == K::Play) TextField("Clip##anim", c.name, "a Character clip, model clip or built-in animation");
    else if (c.kind == K::GoToState) TextField("State##anim", c.name, "an Animator state");
    else if (c.kind != K::Stop) TextField("Parameter##anim", c.name, "an Animator parameter");
    if (c.kind == K::SetFloat) Drag("Value##anim", c.value, 0.05f, -1e6f, 1e6f);
    if (c.kind == K::SetBool) { bool b = c.value != 0.0f; if (Check("True##anim", b)) c.value = b ? 1.0f : 0.0f; }
    if (ImGui::Button("Delete cue")) DeleteSel(s);
}

void AudioCueInspector(EditorState& ed, State& s) {
    SeqAudioCue& c = s.seq.audio[(std::size_t)s.sel.track].cues[(std::size_t)s.sel.index];
    ImGui::TextColored(AccentCol(1.0f), "Audio cue");
    TimeField(s);
    TextField("File##audio", c.file, "empty = the Audio Source's own clip");
    ImGui::SameLine();
    if (ImGui::SmallButton("...##audio")) {
        const char* filt[] = {"*.wav", "*.ogg", "*.mp3"};
        const std::string dir = ed.projectDir().empty() ? std::string() : ed.projectDir() + "/";
        if (const char* p = tinyfd_openFileDialog("Pick a sound", dir.c_str(), 3, filt, "Audio", 0)) {
            BeginEdit(); c.file = ProjectRelative(ed, p); Touch();
        }
    }
    Drag("Volume##audio", c.volume, 0.01f, 0.0f, 2.0f);
    if (ImGui::Button("Delete cue")) DeleteSel(s);
}

void EventInspector(EditorState& ed, State& s) {
    SeqEvent& e = s.seq.events[(std::size_t)s.sel.track].events[(std::size_t)s.sel.index];
    ImGui::TextColored(AccentCol(1.0f), "Event");
    TimeField(s);
    static const char* kKinds[] = {"Named event", "Set active", "Teleport", "Fade to black", "Fade from black", "Subtitle", "Time scale", "Wait for input", "End"};
    KindCombo("Action##ev", e.kind, kKinds, 9);
    using K = SeqEvent::Kind;
    switch (e.kind) {
    case K::Named:
        TextField("Name##ev", e.name, "reaches on_sequence_event(name)");
        ImGui::TextDisabled("Scripts: on_sequence_event(name).\nVisual scripts: On Sequence Event.\nActions: an On Message trigger.");
        break;
    case K::SetActive: {
        ObjectCombo("Object##ev", e.name, ed, false);
        bool on = e.value != 0.0f;
        if (Check("Active##ev", on)) e.value = on ? 1.0f : 0.0f;
        break;
    }
    case K::Teleport:
        ObjectCombo("Object##ev", e.name, ed, false);
        ObjectCombo("To##ev", e.text, ed, false);
        break;
    case K::FadeOut: case K::FadeIn:
        Drag("Duration##ev", e.value, 0.01f, 0.0f, 30.0f, "%.2f s");
        break;
    case K::Subtitle:
        TextField("Speaker##ev", e.name, "optional");
        TextField("Text##ev", e.text);
        Drag("Duration##ev", e.value, 0.01f, 0.1f, 60.0f, "%.2f s");
        break;
    case K::TimeScale:
        Drag("Scale##ev", e.value, 0.01f, 0.0f, 10.0f, "x%.2f");
        ImGui::TextDisabled("The game's time scale from here on; the\nsequence restores it when it ends.");
        break;
    case K::WaitForInput:
        ImGui::TextDisabled("Holds the playhead until Enter, Space\nor the gamepad's confirm button.");
        break;
    case K::End:
        ImGui::TextDisabled("Ends the sequence here.");
        break;
    }
    if (ImGui::Button("Delete event")) DeleteSel(s);
}

void DrawInspector(EditorState& ed, const std::vector<Row>& rows) {
    State& s = S();
    (void)rows;
    if (SelValid(s)) {
        switch (s.sel.kind) {
        case Kind::Camera:    ShotInspector(ed, s); return;
        case Kind::Transform: TransformKeyInspector(s); return;
        case Kind::Animation: AnimCueInspector(s); return;
        case Kind::Audio:     AudioCueInspector(ed, s); return;
        case Kind::Events:    EventInspector(ed, s); return;
        default: break;
        }
    }
    // Nothing selected: the track row, then the sequence.
    if (s.rowKind != Kind::None) {
        ImGui::TextColored(AccentCol(1.0f), "%s track", KindName(s.rowKind));
        if (s.rowKind == Kind::Transform && s.rowTrack < (int)s.seq.transforms.size()) {
            ObjectCombo("Object##tr", s.seq.transforms[(std::size_t)s.rowTrack].object, ed, false);
            ImGui::TextDisabled("Keys its local position, rotation\nand scale. Record, or press Key.");
        } else if (s.rowKind == Kind::Animation && s.rowTrack < (int)s.seq.animations.size()) {
            ObjectCombo("Object##an", s.seq.animations[(std::size_t)s.rowTrack].object, ed, false);
            ImGui::TextDisabled("Plays clips or drives the Animator\nof that object.");
        } else if (s.rowKind == Kind::Audio && s.rowTrack < (int)s.seq.audio.size()) {
            ObjectCombo("Object##au", s.seq.audio[(std::size_t)s.rowTrack].object, ed, false);
            ImGui::TextDisabled("Plays through that object's Audio\nSource (added when missing).");
        } else if (s.rowKind == Kind::Events && s.rowTrack < (int)s.seq.events.size()) {
            TextField("Name##evt", s.seq.events[(std::size_t)s.rowTrack].name);
            ImGui::TextDisabled("Named events, fades, subtitles,\nwaits and the end.");
        } else if (s.rowKind == Kind::Camera) {
            ImGui::TextDisabled("Shots drive the scene's main camera;\nit comes back when the sequence ends.");
        }
        ImGui::Separator();
    }
    ImGui::TextColored(AccentCol(1.0f), "Sequence");
    TextField("Name##seq", s.seq.name);
    Drag("Length##seq", s.seq.length, 0.05f, 0.0f, 3600.0f, "%.2f s (0 = last entry)");
    Check("Loop##seq", s.seq.loop);
    if (s.seq.loop) {
        Drag("Loop start##seq", s.seq.loopStart, 0.05f, 0.0f, 3600.0f, "%.2f s");
        Drag("Loop end##seq", s.seq.loopEnd, 0.05f, 0.0f, 3600.0f, "%.2f s (0 = length)");
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Duration %.2f s", s.seq.Duration());
    if (!s.path.empty()) { ImGui::TextDisabled("%s", ProjectRelative(ed, s.path).c_str()); }
}

// ---- The toolbar -----------------------------------------------------------------------------
void DrawToolbar(EditorState& ed) {
    State& s = S();
    const ImGuiStyle& sty = ImGui::GetStyle();
    const bool playMode = ed.isPlaying();
    if (ImGui::Button("New")) DoNew();
    ImGui::SameLine();
    if (ImGui::Button("Open...")) {
        const char* filt[] = {"*.okayseq"};
        const std::string dir = ed.projectDir().empty() ? std::string() : ed.projectDir() + "/Assets/";
        if (const char* p = tinyfd_openFileDialog("Open sequence", dir.c_str(), 1, filt, "OkaySpace sequence", 0)) DoOpen(p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) DoSave(ed, false);
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) DoSave(ed, true);
    ImGui::SameLine();
    ImGui::BeginDisabled(s.undo.empty());
    if (ImGui::Button("Undo")) TimelineUndo(false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.redo.empty());
    if (ImGui::Button("Redo")) TimelineUndo(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", s.path.empty() ? "(unsaved)" : std::filesystem::path(s.path).filename().string().c_str(), s.dirty ? " *" : "");
    if (playMode) {
        ImGui::SameLine();
        SequencePlayer* sp = SequencePlayer::Playing(ed.scene());
        if (sp) { if (ImGui::Button("Stop in Game")) sp->Stop(); }
        else if (ImGui::Button("Play in Game")) PlayInGame(ed);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run this sequence through a Sequence Player in the running game.");
    }

    // The transport row.
    ImGui::BeginDisabled(playMode);
    if (AnimGlyphButton("##tlrec", 0, s.record ? "Recording: moving a tracked object (the gizmo, the Inspector) keys it at the playhead. Click to stop."
                                               : "Record: changes to a tracked object are keyed at the playhead", s.record, IM_COL32(200, 50, 50, 255)))
        s.record = !s.record;
    ImGui::SameLine(0, sty.ItemSpacing.x * 1.5f);
    const std::vector<float> times = AllTimes(s.seq);
    const int tr = AnimTransport(s.playing);
    if (tr == 0) { s.t = 0.0f; s.playing = false; }
    else if (tr == 1) { float best = 0.0f; for (float k : times) if (k < s.t - 1e-3f) best = k; s.t = best; s.playing = false; }
    else if (tr == 2) s.playing = !s.playing;
    else if (tr == 3) { for (float k : times) if (k > s.t + 1e-3f) { s.t = k; break; } s.playing = false; }
    else if (tr == 4) { s.t = s.seq.Duration(); s.playing = false; }
    ImGui::SameLine(0, sty.ItemSpacing.x * 1.5f);
    if (AnimGlyphButton("##tlloop", 13, "Loop between the markers on the ruler", s.seq.loop)) { BeginEdit(); s.seq.loop = !s.seq.loop; Touch(); }
    ImGui::SameLine();
    if (AnimGlyphButton("##tlkey", 6, "Key the tracked objects as they stand, at the playhead (K)", false)) KeyNow(ed);
    ImGui::SameLine();
    if (AnimGlyphButton("##tlfit", 15, "Fit the whole sequence", false)) AnimTimeViewFit(s.view, std::max(s.seq.Duration(), 2.0f));
    ImGui::EndDisabled();
    ImGui::SameLine(0, sty.ItemSpacing.x * 2.0f);
    ImGui::SetNextItemWidth(90.0f);
    float t = s.t;
    if (ImGui::DragFloat("##tltime", &t, 0.01f, 0.0f, 3600.0f, "%.2f s")) { s.t = Snapped(t); s.playing = false; }
    ImGui::SameLine();
    ImGui::TextDisabled("/ %.2f s", s.seq.Duration());
    ImGui::SameLine(0, sty.ItemSpacing.x * 2.0f);
    ImGui::Checkbox("Snap", &s.snap);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    ImGui::InputInt("fps", &s.fps, 0, 0);
    s.fps = std::clamp(s.fps, 1, 240);
    if (s.record) {
        ImGui::SameLine(0, sty.ItemSpacing.x * 2.0f);
        ImGui::TextColored(ImVec4(0.92f, 0.3f, 0.3f, 1.0f), "REC");
    }
}

} // namespace

// ---- The window --------------------------------------------------------------------------------
bool TimelineFocused() { return g_showTimeline && g_focused; }

void OpenTimelineFile(const std::string& path) { DoOpen(path); }

void StopTimelinePreview() {
    State& s = S();
    ReleasePreview();
    s.playing = false;
}

bool TimelineUndo(bool redo) {
    State& s = S();
    std::vector<Sequence>& from = redo ? s.redo : s.undo;
    std::vector<Sequence>& to = redo ? s.undo : s.redo;
    if (from.empty()) return false;
    to.push_back(s.seq);
    s.seq = from.back();
    from.pop_back();
    s.sel = Sel{};
    ++s.version; s.dirty = true;
    return true;
}

bool TimelinePreviewOverlay(SeqOverlay& out) {
    const State& s = S();
    if (!g_showTimeline || !s.overlayValid) return false;
    out = s.overlay;
    return true;
}

void DrawTimeline(EditorState& ed) {
    State& s = S();
    g_focused = false;
    if (!g_showTimeline) { if (s.binding.Bound()) ReleasePreview(); s.overlayValid = false; return; }
    if (std::getenv("OKAY_UI_TIMELINE")) {
        ImGui::SetNextWindowPos(ImVec2(170, 430), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1260, 410), ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowSize(ImVec2(1100, 420), ImGuiCond_FirstUseEver);
    }
    if (!ImGui::Begin("Timeline", &g_showTimeline)) { ImGui::End(); if (s.binding.Bound()) ReleasePreview(); return; }
    g_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Edit-mode playback; in Play the game's player drives the readout.
    if (ed.isPlaying()) {
        s.playing = false;
        if (SequencePlayer* sp = SequencePlayer::Playing(ed.scene())) s.t = sp->Time();
    } else if (s.playing) {
        s.t += ImGui::GetIO().DeltaTime;
        const float end = s.seq.loop ? s.seq.LoopEndTime() : s.seq.Duration();
        if (s.t >= end) {
            if (s.seq.loop && end > s.seq.loopStart) s.t = s.seq.loopStart + std::fmod(s.t - end, end - s.seq.loopStart);
            else { s.t = end; s.playing = false; }
        }
    }

    DrawToolbar(ed);
    ImGui::Separator();
    const std::vector<Row> rows = Rows(s.seq);
    const float bodyH = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##tltracks", ImVec2(kLeftW, bodyH), ImGuiChildFlags_Borders);
    DrawTracks(ed, rows);
    ImGui::EndChild();
    ImGui::SameLine();
    const float gridW = std::max(120.0f, ImGui::GetContentRegionAvail().x - kRightW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::BeginChild("##tlgridc", ImVec2(gridW, bodyH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);
    DrawGrid(ed, rows);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##tlinsp", ImVec2(0, bodyH), ImGuiChildFlags_Borders);
    DrawInspector(ed, rows);
    ImGui::EndChild();

    // Keys while the window owns them (not while typing in a field).
    if (g_focused && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ed.isPlaying()) s.playing = !s.playing;
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) { s.t = 0.0f; s.playing = false; }
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) { s.t = s.seq.Duration(); s.playing = false; }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) DeleteSel(s);
        if (ImGui::IsKeyPressed(ImGuiKey_K, false) && !ed.isPlaying()) KeyNow(ed);
    }
    PreviewTick(ed);
    ImGui::End();
}

} // namespace oked
