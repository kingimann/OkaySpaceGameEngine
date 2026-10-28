#pragma once
// ---------------------------------------------------------------------------
// AnimBackend — what the Animator (AnimStateMachine) plays its states through.
// ModelAnimator implements it for imported models (clips as per-node tracks);
// CharacterAnimBackend for the built-in Character body (AnimClips posing its
// 15 bones). The state machine — states, transitions, blend trees, layers,
// parameters — is the same either way.
// ---------------------------------------------------------------------------
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace okay {

class AnimStateMachine;
class Component;

class AnimBackend {
public:
    virtual ~AnimBackend() = default;

    /// Index of a clip by name (-1 = none), and its length in seconds.
    virtual int   FindClip(const std::string& name) const = 0;
    virtual float ClipLength(int clip) const = 0;
    /// The clip a single-clip state is playing (-1 = none).
    virtual int   ActiveClip() const = 0;
    /// True when a non-looping single clip has reached its end.
    virtual bool  ClipFinished() const = 0;

    virtual void  SetSpeed(float speed) = 0;
    virtual void  SetLoop(bool loop) = 0;
    /// A blend tree poses the body through BlendClips / BlendMany each frame
    /// (true), or a single clip plays on its own clock (false).
    virtual void  SetExternalDrive(bool on) = 0;

    /// Switch the single clip, crossfading from the current pose over `blend`
    /// seconds (< 0 = the backend's default). False if there's no such clip.
    virtual bool  Play(const std::string& name, float blend = -1.0f) = 0;
    /// Seek the single clip to `t` seconds.
    virtual void  SetTime(float t) = 0;

    /// Start a crossfade from the current pose into a blend tree of these clips.
    virtual void  CaptureCrossfade(int A, int B, float seconds) = 0;
    virtual void  CaptureCrossfadeMany(const std::vector<int>& clips, float seconds) = 0;
    /// The blend trees' shared normalized phase (0..1).
    virtual float BlendPhase() const = 0;
    virtual void  ResetBlendPhase(float p = 0.0f) = 0;
    /// One-shots start at phase 0. Looping trees keep ResetBlendPhase, which may
    /// carry the previous cycle so the feet stay in step.
    virtual void  RestartBlendPhase() { ResetBlendPhase(0.0f); }
    /// Pose a mix of clips A and B (w = weight of B) at one shared phase, or of
    /// any number of clips by `weights`. False if none of the clips exists.
    virtual bool  BlendClips(float dt, int A, int B, float w) = 0;
    virtual bool  BlendMany(float dt, const std::vector<int>& clips, const std::vector<float>& weights) = 0;

    /// Mix (clip, time) samples by `weights` over the current pose by `amount`,
    /// on `maskBone` and its children, plus `moreBones` (each a subtree). An
    /// empty mask is the whole body. `additive` adds the clip on top of the
    /// pose already there (a recoil or aim offset) instead of replacing it.
    virtual void  PoseLayer(const std::vector<std::pair<int, float>>& clipTimes, const std::vector<float>& weights,
                            const std::string& maskBone, float amount, bool additive = false,
                            const std::vector<std::string>* moreBones = nullptr) = 0;
    /// Run `fn` every frame after the base pose (layers register here); the
    /// owner calls RemovePostPose from its OnDestroy.
    virtual void  AddPostPose(const void* owner, std::function<void(float)> fn) = 0;
    virtual void  RemovePostPose(const void* owner) = 0;

    /// Report an animation event (anim_event(), OnAnimationEvent handlers).
    virtual void  FireEvent(const std::string& name) = 0;
    /// The base state machine driving this backend (its own clip selection waits).
    virtual void  SetStateMachine(const Component* sm) = 0;
    /// Once a frame after the state machine's update: a backend that has no
    /// update of its own advances its single clip and crossfade here.
    virtual void  Advance(float dt) { (void)dt; }
    /// Once a frame before the state machine tests its transitions: a backend
    /// whose body has its own idea of the state (the Character's `anim`) syncs
    /// the machine with it here.
    virtual void  BeginFrame(AnimStateMachine& sm) { (void)sm; }
};

} // namespace okay
