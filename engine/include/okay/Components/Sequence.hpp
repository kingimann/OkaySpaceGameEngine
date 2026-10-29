#pragma once
// A sequence: the timeline a cutscene plays from, saved as a .okayseq text asset.
// Its tracks hold camera shots, transform keys, animation cues, audio cues and
// events. A SequencePlayer component plays one in a game; the editor's Timeline
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
#include "okay/Math/Easing.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>
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

/// What the screen overlay shows at a time: the fade to black and the subtitle.
struct SeqOverlay {
    float fade = 0.0f;             ///< 0 clear .. 1 black
    std::string speaker, text;     ///< the subtitle showing ("" = none)
    float subtitle = 0.0f;         ///< its opacity
    bool  waiting = false;         ///< held on a wait-for-input (shows the confirm hint)
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

    /// `length`, or the time of the last entry on any track when it is 0.
    float Duration() const;
    /// The time of the last entry on any track (0 when empty).
    float LastEntryTime() const;
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
    /// The fade and subtitle at `t`, from the event tracks.
    SeqOverlay OverlayAt(float t) const;

    // ---- Editing helpers (the Timeline window; channels stay sorted) ----
    /// Index of the key at `t` (within `eps`), or -1.
    static int KeyAt(const std::vector<SeqKey>& keys, float t, float eps = 1e-3f);
    /// Set a key at `t`: replaces the key there (keeping its easing) or inserts in time order.
    static int SetKey(std::vector<SeqKey>& keys, float t, const Vec3& value, float eps = 1e-3f);
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
    void Bind(Scene& scene, const Sequence& seq);
    void Unbind() { m_scene = nullptr; m_objects.clear(); m_camera.clear(); }
    bool Bound() const { return m_scene != nullptr; }
    Scene* scene() const { return m_scene; }
    /// Write the transforms and the camera of `seq` at `t`.
    void Apply(const Sequence& seq, float t);
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

} // namespace okay
