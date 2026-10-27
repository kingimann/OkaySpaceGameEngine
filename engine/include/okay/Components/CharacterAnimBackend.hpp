#pragma once
// ---------------------------------------------------------------------------
// CharacterAnimBackend — the Animator's backend for the built-in Character body.
// It plays the Character's registered clips (AnimClip: per-bone Euler keys and
// an optional root track): a single clip on its own clock, or a blend tree's
// clips at one shared phase, crossfading from the live pose on every switch.
// The Character shows the result — skins its mesh, drives its part rig, or
// feeds a HumanoidRetarget. Owned by the AnimStateMachine, so the body has one
// animation component.
//
// Clips are the Character's own, then the default humanoid library. The body's
// movement arrives as the machine's parameters (speed, run, crouch, prone,
// airTime, verticalSpeed from a movement controller; gaitX, gaitZ, gaitSpeed
// every frame). `anim` keeps working: it reads the state playing (by name:
// "Walk" is 2), and a write from outside (a script, the network, a controller
// that sets it itself) moves the machine to that state and keeps it there until
// a movement controller reports again.
// ---------------------------------------------------------------------------
#include "okay/Components/AnimBackend.hpp"
#include "okay/Components/Character.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

class GameObject;

class CharacterAnimBackend : public AnimBackend, public Character::PoseSource {
public:
    explicit CharacterAnimBackend(GameObject* owner) : m_owner(owner) {}
    CharacterAnimBackend(const CharacterAnimBackend&) = delete;
    CharacterAnimBackend& operator=(const CharacterAnimBackend&) = delete;
    /// Stop driving the Character (the Animator was disabled or removed); the
    /// next Advance takes it over again.
    void Detach();

    int   FindClip(const std::string& name) const override;
    float ClipLength(int clip) const override;
    int   ActiveClip() const override { return m_cur.blend ? -1 : m_cur.clip; }
    bool  ClipFinished() const override;
    void  SetSpeed(float speed) override;
    void  SetLoop(bool loop) override { m_loop = loop; }
    void  SetExternalDrive(bool) override {}   // BlendClips / Play say which kind of state plays
    bool  Play(const std::string& name, float blend = -1.0f) override;
    void  SetTime(float t) override;
    void  CaptureCrossfade(int A, int B, float seconds) override;
    void  CaptureCrossfadeMany(const std::vector<int>& clips, float seconds) override;
    float BlendPhase() const override { return m_cur.phase; }
    /// A blend tree entered from another one at offset 0 carries on at the same
    /// phase, so the feet stay in step (walk -> run -> crouch-walk).
    void  ResetBlendPhase(float p = 0.0f) override;
    bool  BlendClips(float dt, int A, int B, float w) override;
    bool  BlendMany(float dt, const std::vector<int>& clips, const std::vector<float>& weights) override;
    void  PoseLayer(const std::vector<std::pair<int, float>>& clipTimes, const std::vector<float>& weights,
                    const std::string& maskBone, float amount, bool additive = false,
                    const std::vector<std::string>* moreBones = nullptr) override;
    void  AddPostPose(const void* owner, std::function<void(float)> fn) override;
    void  RemovePostPose(const void* owner) override;
    void  FireEvent(const std::string& name) override;
    void  SetStateMachine(const Component* sm) override { m_machine = sm; }
    void  Advance(float dt) override;
    void  BeginFrame(AnimStateMachine& sm) override;

    bool  AnimatorPose(std::vector<Vec3>& pose, Vec3& root) override;
    bool  Live() const override;
    void  Move(const Movement& m) override;
    void  Gait(float x, float z, float speed) override;

private:
    /// What one side of a crossfade plays.
    struct Playing {
        bool  blend = false;              ///< a blend tree (clips at one phase) or one clip
        int   clip = -1;                  ///< single clip
        float time = 0.0f;                ///< its clock (seconds of playback)
        bool  loop = true;                ///< it loops (else it holds its last frame)
        std::vector<int>   clips;         ///< blend tree clips ...
        std::vector<float> weights;       ///< ... and their weights (sum 1)
        float phase = 0.0f;               ///< the blend's shared normalized phase
        float cycle = 0.0f;               ///< the blend's cycle length (s)
        float rate = 1.0f;                ///< playback speed it last ran at
    };
    Character* Body() const;
    const std::vector<std::string>& Names() const;   // clip index -> name, refreshed when the clips change
    const AnimClip* Clip(int i) const;
    void Sample(const Playing& p, std::vector<Vec3>& pose, Vec3& root) const;
    void Step(Playing& p, float dt) const;           // advance a side's clock
    void StartFade(float seconds);                   // the playback so far fades out live
    void FireWindow(int clip, float t0, float t1);   // events crossed between clip times t0 -> t1

    GameObject* m_owner;
    const Component* m_machine = nullptr;            // the state machine playing through us
    AnimStateMachine* m_sm = nullptr;                // the same, for its parameters and state
    int   m_published = -1;                          // the `anim` we last wrote (-1 = none yet)
    bool  m_forced = false;                          // an outside `anim` write holds the state ...
    bool  m_moved = false;                           // ... until a movement controller reports again
    bool  m_off = false;                             // `anim` 0: the body holds its own pose
    bool  m_carry = false;                           // the tree just left, for ResetBlendPhase ...
    float m_carryPhase = 0.0f;                       // ... and its phase
    mutable std::vector<std::string> m_names;        // Character::ClipNames, sorted
    mutable std::uint32_t m_namesVer = ~0u;
    mutable Playing m_cur, m_from;                   // (their clips are re-found when the list changes)
    bool  m_fromLive = false;                        // the fade's source still plays (else frozen)
    std::vector<Vec3> m_fromPose;
    Vec3  m_fromRoot{0, 0, 0};
    float m_fadeT = 0.0f, m_fadeDur = 0.0f;
    float m_speed = 1.0f;
    bool  m_loop = true;
    bool  m_fromStart = true;                        // events at the clip's very start still fire
    struct PostPose { const void* owner; std::function<void(float)> fn; };
    std::vector<PostPose> m_post;
    std::vector<Vec3> m_pose;                        // this frame's pose ...
    Vec3  m_root{0, 0, 0};                           // ... and body offset
    bool  m_have = false;                            // a pose was made
    bool  m_final = true;                            // the post-pose hooks ran on it
    float m_dt = 0.0f;
};

} // namespace okay
