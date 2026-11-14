#pragma once
// A sequence: the timeline a cutscene plays from, saved as a .okayseq text asset.
// Its tracks hold camera shots, transform keys, animation cues, audio cues,
// events, letterbox bars, camera shake, a coloured screen fade and nested
// sequences. A SequencePlayer component plays one in a game; the editor's Timeline
// window edits one and previews it at the playhead. Everything here is plain data
// plus pure evaluation at a time, so the preview and the game agree.
//
// Text format (see docs/cutscenes.md):
//   okayseq 1
//   sequence "Intro" length 8 loop 0 0 0
//   camera
//     shot 0 cam "Shot A" blend 0 ease linear          # a camera object's pose
//     shot 3 pose 0 2 6 -10 180 0 fov 50 blend 1 ease sineinout   # an inline pose
//   transform "Hero"
//     pos 0 0 0 0 linear                                # time x y z easing
//     rot 2 0 90 0 sineinout                            # Euler degrees
//   animation "Hero"
//     play 0.5 "wave"    stop 3    float 1 "speed" 2.5    bool 1 "armed" 1
//     trigger 2 "attack"    state 2.5 "Idle"
//   audio "Hero"
//     cue 1 "sfx/door.wav" 0.8                          # file (""= the source's clip) volume
//   events "Main"
//     event 1 "door_opened"    active 2 "Door" 0    teleport 2.5 "Hero" "Spawn"
//     fadeout 3 1    fadein 4 1    subtitle 4 "Guide" "Welcome." 3
//     timescale 5 0.5    wait 6    end 8
//   letterbox
//     color 0 0 0 1
//     bar 0 0 linear          bar 1 0.12 sineinout     # time, fraction of screen height, curve
//   shake
//     strength 0 0 linear     strength 1 0.8 sineinout # 0 still .. 1 full
//     frequency 0 14 linear                            # shakes per second
//   fade
//     rgba 0 0 0 0 1 sineinout                         # time r g b opacity curve
//   nest "Door"
//     play 2 "glance.okayseq" 0                        # time, file, seconds to skip into it
#include "okay/Math/Easing.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <utility>
#include <vector>

namespace okay {

class Scene;
class GameObject;
class Camera;

/// A camera shot: from `time` the main camera takes a scene camera's pose (by
/// object name) or the inline pose, cutting (blend 0) or easing in from the
/// previous shot over `blend` seconds.
struct SeqShot {
    float time = 0.0f;
    std::string camera;            ///< a camera object's name ("" = the inline pose)
    Vec3  position{0, 0, 0};       ///< inline pose, world units
    Vec3  rotation{0, 0, 0};       ///< inline pose, Euler degrees
    float fov = 0.0f;              ///< vertical field of view (0 = keep the camera's)
    float blend = 0.0f;            ///< seconds eased in from the previous shot (0 = cut)
    Ease  ease = Ease::SineInOut;
};

/// One key of a transform channel; `ease` shapes the span to the next key.
struct SeqKey {
    float time = 0.0f;
    Vec3  value{0, 0, 0};
    Ease  ease = Ease::Linear;
};

/// Local position / rotation (Euler degrees) / scale keys of one object. A channel
/// without keys leaves that part of the transform alone.
struct SeqTransformTrack {
    std::string object;            ///< the object's name, or a "Parent/Child" path
    std::vector<SeqKey> position, rotation, scale;
};

/// An animation cue: play or stop a clip on what the object animates (a Character
/// clip, a model clip or a built-in animation), or drive its Animator.
struct SeqAnimCue {
    enum class Kind { Play, Stop, SetFloat, SetBool, Trigger, GoToState };
    float time = 0.0f;
    Kind  kind = Kind::Play;
    std::string name;              ///< the clip, parameter or state
    float value = 0.0f;            ///< SetFloat's value; SetBool's 0 / 1
};
struct SeqAnimTrack { std::string object; std::vector<SeqAnimCue> cues; };

/// An audio cue: `file` played once on the object's AudioSource ("" = the
/// source's own clip, restarted), at `volume`.
struct SeqAudioCue { float time = 0.0f; std::string file; float volume = 1.0f; };
struct SeqAudioTrack { std::string object; std::vector<SeqAudioCue> cues; };

/// An event: a named event for scripts, or a built-in action.
struct SeqEvent {
    enum class Kind { Named, SetActive, Teleport, FadeOut, FadeIn, Subtitle, TimeScale, WaitForInput, End };
    float time = 0.0f;
    Kind  kind = Kind::Named;
    std::string name;              ///< Named: the event; SetActive / Teleport: the object; Subtitle: the speaker
    std::string text;              ///< Teleport: the destination object; Subtitle: the line
    float value = 0.0f;            ///< SetActive: 1 on / 0 off; fades and subtitles: seconds; TimeScale: the scale
};
struct SeqEventTrack { std::string name = "Events"; std::vector<SeqEvent> events; };

/// One number at a time. `ease` shapes the span to the next key (held at the ends).
struct SeqFloatKey {
    float time = 0.0f;
    float value = 0.0f;
    Ease  ease = Ease::Linear;
};

/// A colour and its opacity at a time. `ease` shapes the span to the next key.
struct SeqColorKey {
    float time = 0.0f;
    Color color{0.0f, 0.0f, 0.0f, 0.0f};
    Ease  ease = Ease::Linear;
};

/// Letterbox bars: top and bottom strips. Each key's value is the fraction of
/// the screen height one bar covers (0 none, about 0.12 a film bar, clamped to 0.45).
struct SeqLetterboxTrack {
    bool on = false;
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<SeqFloatKey> keys;
};

/// Camera shake, keyed so scrubbing and playback agree. Strength is 0 still to
/// 1 the camera's full shake distance; frequency is shakes per second (18 when
/// that channel is empty). Reduced motion makes the strength 0.
struct SeqShakeTrack {
    bool on = false;
    std::vector<SeqFloatKey> strength;
    std::vector<SeqFloatKey> frequency;
};

/// A full-screen colour fade, separate from the event track's fade to black.
/// Drawn as an overlay, so every renderer shows it.
struct SeqFadeTrack {
    bool on = false;
    std::vector<SeqColorKey> keys;
};

/// One play of another sequence. At `time` the child is at `offset` seconds,
/// then both advance together until the child ends.
struct SeqNestCue {
    float time = 0.0f;
    std::string path;              ///< the .okayseq file, as written
    float offset = 0.0f;           ///< seconds into the child when this cue starts
};
struct SeqNestTrack { std::string name = "Nested"; std::vector<SeqNestCue> cues; };

/// What the screen overlay shows at a time: fades, letterbox bars and the subtitle.
struct SeqOverlay {
    float fade = 0.0f;             ///< 0 clear .. 1 black (the event track's fade)
    Color screen{0.0f, 0.0f, 0.0f, 0.0f}; ///< the fade track: rgb colour, a is the opacity
    float letterbox = 0.0f;        ///< fraction of the screen height, each bar (0 = none)
    Color letterboxColor{0.0f, 0.0f, 0.0f, 1.0f};
    std::string speaker, text;     ///< the subtitle showing ("" = none)
    float subtitle = 0.0f;         ///< its opacity
    bool  waiting = false;         ///< held on a wait-for-input (shows the confirm hint)
};

class SeqLibrary;

/// One thing a sequence fires as the playhead crosses it (its own, or a nested
/// sequence's, already moved onto the parent's clock).
struct SeqFired {
    float time = 0.0f;
    const SeqEvent* event = nullptr;
    const SeqAnimTrack* animTrack = nullptr;
    const SeqAnimCue* anim = nullptr;
    const SeqAudioTrack* audioTrack = nullptr;
    const SeqAudioCue* audio = nullptr;
};

/// The camera a sequence asks for at a time.
struct SeqCameraPose {
    bool  valid = false;
    Vec3  position{0, 0, 0};
    Quat  rotation = Quat::Identity;
    float fov = 0.0f;              ///< 0 = keep
};

struct Sequence {
    std::string name = "Sequence";
    float length = 0.0f;           ///< seconds (0 = up to the last entry)
    bool  loop = false;
    float loopStart = 0.0f, loopEnd = 0.0f;   ///< loop markers (end 0 = the duration)
    bool  cameraTrack = false;     ///< the camera track exists (it may still be empty)
    std::vector<SeqShot> shots;    ///< in time order
    std::vector<SeqTransformTrack> transforms;
    std::vector<SeqAnimTrack> animations;
    std::vector<SeqAudioTrack> audio;
    std::vector<SeqEventTrack> events;
    SeqLetterboxTrack letterbox;
    SeqShakeTrack shake;
    SeqFadeTrack fade;
    std::vector<SeqNestTrack> nests;

    /// `length`, or the time of the last entry on any track when it is 0.
    /// With `lib`, a nested sequence stretches that to the child's end.
    float Duration(const SeqLibrary* lib = nullptr) const;
    /// The time of the last entry on any track (0 when empty).
    float LastEntryTime(const SeqLibrary* lib = nullptr) const;
    /// Where a loop wraps: `loopEnd` when set and within the duration, else the duration.
    float LoopEndTime() const;
    /// Put every track's entries in time order (stable, so equal times keep their order).
    void Sort();

    // ---- Evaluation (pure: the same answer for the same `t`) ----
    /// The camera at `t`: the shot in effect, blended from the previous one while
    /// its blend runs. `before` is the camera's pose when the sequence started (what
    /// the first shot blends from); object shots read `scene`'s cameras. A shot
    /// naming `drivenCamera` (the camera the sequence itself is moving) gives
    /// `before`: its live transform is the sequence's own last write.
    SeqCameraPose CameraAt(float t, const Scene* scene, const SeqCameraPose& before,
                           const std::string& drivenCamera = std::string()) const;
    /// The pose one shot asks for on its own (no blend); `before` for a missing
    /// object or the driven camera.
    static SeqCameraPose ShotPose(const SeqShot& shot, const Scene* scene, const SeqCameraPose& before,
                                  const std::string& drivenCamera = std::string());
    /// A channel's value at `t` (held at the ends, eased between keys). False when empty.
    static bool ChannelAt(const std::vector<SeqKey>& keys, float t, Vec3& out);
    /// A rotation channel at `t`: Euler keys slerped along the short arc. False when empty.
    static bool RotationAt(const std::vector<SeqKey>& keys, float t, Quat& out);
    /// A number channel at `t` (held at the ends, eased between keys). False when empty.
    static bool FloatAt(const std::vector<SeqFloatKey>& keys, float t, float& out);
    /// A colour channel at `t`, opacity included. False when empty.
    static bool ColorAt(const std::vector<SeqColorKey>& keys, float t, Color& out);
    /// The fade, letterbox, coloured screen and subtitle at `t`. Nested sequences
    /// in `lib` composite on top (a missing library ignores them).
    SeqOverlay OverlayAt(float t, const SeqLibrary* lib = nullptr) const;
    /// Camera-local shake at `t` before the camera's own distance and angle:
    /// about -1..1 times the strength. Multiply position by `shakeAmount` and
    /// rotation (degrees) by `shakeAngle`. Zero when the track is off, the
    /// strength is 0, or reduced motion is on. Nested shakes add.
    void ShakeAt(float t, const SeqLibrary* lib, Vec3& outPos, Vec3& outRotDeg) const;
    /// Events, animation cues and audio cues crossed in (a, b] (or [a, b] with
    /// `includeA`), including the ones inside a nested sequence, on this clock.
    void CollectFired(float a, float b, bool includeA, const SeqLibrary* lib, std::vector<SeqFired>& out) const;

    // ---- Editing helpers (the Timeline window; channels stay sorted) ----
    /// Index of the key at `t` (within `eps`), or -1.
    static int KeyAt(const std::vector<SeqKey>& keys, float t, float eps = 1e-3f);
    static int FloatKeyAt(const std::vector<SeqFloatKey>& keys, float t, float eps = 1e-3f);
    static int ColorKeyAt(const std::vector<SeqColorKey>& keys, float t, float eps = 1e-3f);
    /// Set a key at `t`: replaces the key there (keeping its easing) or inserts in time order.
    static int SetKey(std::vector<SeqKey>& keys, float t, const Vec3& value, float eps = 1e-3f);
    static int SetFloatKey(std::vector<SeqFloatKey>& keys, float t, float value, float eps = 1e-3f);
    static int SetColorKey(std::vector<SeqColorKey>& keys, float t, const Color& color, float eps = 1e-3f);
    /// `t` snapped to the nearest frame at `fps` (fps <= 0: unchanged).
    static float SnapToFrame(float t, int fps);
    /// An object by name, or by a "Parent/Child" path from a root object.
    static GameObject* FindObject(const Scene& scene, const std::string& path);

    // ---- Text ----
    static bool Parse(const std::string& text, Sequence& out, std::string* error = nullptr);
    std::string Write() const;
    static bool LoadFromFile(const std::string& path, Sequence& out, std::string* error = nullptr);
    bool SaveToFile(const std::string& path, std::string* error = nullptr) const;
    /// The easing word the text format uses ("sineinout"), and back.
    static std::string EaseWord(Ease e);
};

/// Applies a sequence to a scene at a time and remembers what it touched, so it
/// can hand things back: the SequencePlayer returns the camera when the sequence
/// ends (moved objects stay where the cutscene left them), the editor's preview
/// returns everything when it stops. Objects are found by name each time, so a
/// scene that changed under it never leaves a dangling pointer.
class SequenceBinding {
public:
    /// Remember the camera and the tracked objects' transforms as they are now.
    /// `lib` is the nested sequences, so their objects are remembered too.
    void Bind(Scene& scene, const Sequence& seq, const SeqLibrary* lib = nullptr);
    void Unbind();
    bool Bound() const { return m_scene != nullptr; }
    Scene* scene() const { return m_scene; }
    /// Write the transforms, the camera and the shake of `seq` at `t`.
    void Apply(const Sequence& seq, float t, const SeqLibrary* lib = nullptr);
    /// Put the main camera back as it was at Bind.
    void RestoreCamera();
    /// Put the camera and every tracked object back as they were at Bind.
    void RestoreAll();
    /// The camera being driven (null without a camera track or a camera).
    Camera* camera() const;
    /// The camera's world pose at Bind (what the first shot blends from).
    const SeqCameraPose& before() const { return m_before; }

private:
    struct Saved { std::string path; Vec3 position; Quat rotation; Vec3 scale; };
    Scene* m_scene = nullptr;
    std::vector<Saved> m_objects;
    std::string m_camera;          ///< the main camera object's name at Bind
    Vec3  m_camPos{0, 0, 0}; Quat m_camRot = Quat::Identity; float m_camFov = 60.0f;   ///< its local pose then
    SeqCameraPose m_before;
};

/// The nested sequences a cutscene plays, loaded from the files its nest cues
/// name. Not saved inside the sequence (the file path is). A cue's path finds
/// the child; a file that nests itself is skipped.
class SeqLibrary {
public:
    void Clear() { m_items.clear(); }
    bool empty() const { return m_items.empty(); }
    /// Keep `seq` under `key` (the path a cue writes). Replaces a key already there.
    void Add(const std::string& key, Sequence seq);
    /// The sequence stored as `key`, or the same file name. Null when missing.
    const Sequence* Find(const std::string& key) const;
    /// Read every nest cue under `root`, relative to `fromFile`'s folder (then the
    /// project folder). A missing file or a cycle is skipped; `note` says why
    /// the first one was, and the rest of the sequence still loads.
    void LoadFrom(const Sequence& root, const std::string& fromFile, std::string* note = nullptr);

private:
    std::vector<std::pair<std::string, Sequence>> m_items;
};

} // namespace okay
