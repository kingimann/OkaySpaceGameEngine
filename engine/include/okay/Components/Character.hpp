#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Components/AnimClip.hpp"
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>

namespace okay {

/// A blocky, flat-shaded low-poly character creator (Unturned style) with deep
/// customization: one height slider, a big palette of colors, and lots of
/// clothing / hair / accessory options. The parts are bound to a 15-bone humanoid
/// skeleton (rigid skinning) so the character can be posed and plays procedural
/// animations (idle / walk / run / wave / jump). Lives next to a MeshRenderer and
/// rebuilds its mesh when changed (like Terrain).
class Character : public Component {
public:
    // ---- Proportions ----
    float height = 1.0f;     // overall height multiplier

    // ---- Colors ----
    Color skin   = Color::FromBytes(220, 176, 150);
    Color shirt  = Color::FromBytes(60, 95, 75);
    Color pants  = Color::FromBytes(40, 42, 55);
    Color shoes  = Color::FromBytes(38, 35, 38);
    Color hair   = Color::FromBytes(80, 50, 30);
    Color eyes   = Color::FromBytes(30, 26, 28);
    Color hat    = Color::FromBytes(150, 45, 45);
    Color pack   = Color::FromBytes(75, 60, 45);
    Color gloves = Color::FromBytes(45, 40, 35);
    Color jacket = Color::FromBytes(70, 70, 80);
    Color belt   = Color::FromBytes(35, 28, 22);

    // ---- Hair & facial hair ----
    bool hasHair    = true;
    int  hairStyle  = 0;     // 0 short,1 long,2 mohawk,3 bun,4 spiky,5 afro,6 ponytail,7 buzz
    int  beardStyle = 0;     // 0 none,1 full,2 goatee,3 mustache

    // ---- Clothing ----
    int  shirtStyle = 2;     // 0 tank (bare arms),1 short sleeve,2 long sleeve
    int  legStyle   = 0;     // 0 trousers,1 shorts
    bool hasBelt    = true;
    bool hasJacket  = false;
    bool hasGloves  = false;

    // ---- Accessories ----
    bool hasHat        = false;
    int  hatStyle      = 0;  // 0 cap,1 helmet,2 top hat,3 wizard,4 beanie,5 cowboy,6 crown,7 bandana
    int  glassesStyle  = 0;  // 0 none,1 glasses,2 sunglasses
    bool hasMask       = false;
    bool hasScarf      = false;
    bool hasShoulderPads = false;
    bool hasBackpack   = false;
    bool hasCape       = false;

    // ---- Animation (plays in Play mode) ----
    int   anim      = 1;     // 0 none,1 idle,2 walk,3 run,4 wave,5 jump,6 crouch,7 prone,
                             // 8 point,9 clap,10 thumbs-up,11 salute,12 wave-both,
                             // 13 cheer/happy,14 sad,15 angry,16 think (gestures + emotions),
                             // 17 crouch-walk, 18 fall, 19 hang (arms up, legs dangling)
    float animSpeed = 1.0f;
    float animTime  = 0.0f;  // runtime clock (not serialized)

    // ---- Locomotion (walk / run / crouch-walk / prone crawl) ----
    // The built-in gait is laid out from how fast the body really moves over the
    // ground, so the planted foot stays put instead of skating: the stride, cadence,
    // hip drop and bounce all follow `moveSpeed`. Controllers set these every frame;
    // they are not serialized.
    //   moveSpeed  ground speed in m/s. < 0 = not reported: the cycle plays at a
    //              natural pace for the state (an NPC set to walk walks on the spot).
    //   moveAngle  direction of travel relative to the way the body faces, degrees,
    //              + = toward the body's right. The hips and legs turn toward it
    //              (strafing) and step backwards when it points behind (backpedal),
    //              while the chest keeps facing forward.
    float moveSpeed = -1.0f;
    float moveAngle = 0.0f;
    /// Where the gait cycle is, 0..1 (0 = left foot touching down, 0.5 = right foot).
    float GaitPhase() const;

    /// What a movement controller knows about the body this frame.
    struct Locomotion {
        Vec3  velocity;            ///< the body's world velocity (m/s)
        Vec3  facing{0, 0, -1};    ///< the body's forward direction (world)
        bool  onGround = true;     ///< touching the ground (a contact, not just slow vertically)
        bool  jumped = false;      ///< a jump started this frame
        bool  crouch = false, prone = false;
        bool  sprint = false;      ///< the run input is held
        float walkSpeed = 4.5f;    ///< the controller's walk speed (run/walk split)
        float dt = 0.0f;
        /// Movement along this axis isn't walking (2.5D: the hop between depth
        /// layers slides along Z); zero = count every direction.
        Vec3  ignoreAxis{0, 0, 0};
    };
    /// Pick the movement state from what the body is actually doing — idle / walk /
    /// run by real ground speed (a body pinned against a wall stands instead of
    /// running in place), crouch / crouch-walk / prone, jump while rising and fall
    /// while dropping — and report speed + direction so the stride matches the ground.
    /// The first- and third-person and 2.5D controllers call this every frame.
    void DriveLocomotion(const Locomotion& l);

    // ---- Punch / swing (a quick right-arm arc layered over the current anim) ----
    // Trigger a one-shot punch with Punch(); the right arm swings forward and back
    // over `punchDuration` seconds on top of whatever the body is already doing
    // (walking, idling). Used by the first-person hand so your OWN character's arm
    // does the hitting — no separate floating viewmodel.
    float punchDuration = 0.28f;
    void  Punch() { if (m_punchT < 0.0f || m_punchT >= 1.0f) m_punchT = 0.0f; }
    bool  Punching() const { return m_punchT >= 0.0f && m_punchT < 1.0f; }

    // ---- First-person arm (driven by FirstPersonHand) ----
    // When firstPersonArm is on, the body is frozen (so walking never brings your
    // torso/legs into view) and your own arm is raised into the first-person camera —
    // it's literally your character's arm, not a separate viewmodel. fpArmUp eases
    // 0 (lowered, empty-handed) .. 1 (raised, holding). fpRaise / fpElbow let the
    // FirstPersonHand inspector tune where the hand sits.
    bool  firstPersonArm = false;
    float fpArmUp = 1.0f;
    float fpRaise = -96.0f;   // raised upper-arm forward angle
    float fpElbow = 16.0f;    // raised forearm angle (hand closeness/height)
    /// Which 3-bone arm the first-person view raises into frame (the base bone index,
    /// e.g. 3 = the +X / screen-right arm). -1 = none. Set by FirstPersonHand; the
    /// separated-rig DriveParts poses these bones up like a Minecraft hand and routes
    /// the punch onto them so the VISIBLE arm is the one that swings. Not serialized.
    int   fpArmBase = -1;
    /// Camera pitch (degrees) fed in by FirstPersonHand so the first-person arm tilts
    /// up/down with your view (Minecraft-style). 0 = level. Not serialized.
    float fpPitch = 0.0f;
    /// Let the walk/run cycle bob the first-person arm (the arm inherits the body's
    /// sway). Turn off for a rock-steady arm. Set by FirstPersonHand. Not serialized.
    bool  fpArmBob = true;
    /// Fully decouple the first-person arm from the body: ignore lean, crouch and prone
    /// (zero the hips/torso the arm hangs from and skip the stance height drop) so the
    /// arm stays put in view no matter what the body does. Set by FirstPersonHand.
    bool  fpSteady = false;

    // ---- External rig (retargeting) ----
    /// Compute the animation pose but render NOTHING — no baked mesh, no part
    /// rig. A HumanoidRetarget component next to this Character reads
    /// CurrentPose() each frame and transfers it onto an imported humanoid
    /// skeleton, so a swapped model plays every built-in animation. Serialized.
    bool driveExternal = false;

    // ---- Separate body parts (a real, editable rig) ----
    // Instead of one baked mesh, build the character as a HIERARCHY of part
    // GameObjects (one per bone: Hips, Torso, Head, arms, legs) you can select,
    // recolour, attach things to (a sword in the Hand), and animate part-by-part.
    // The built-in animations still play (they drive the part transforms) unless you
    // turn `animateParts` off to author your own. The first-person hand just hides
    // the non-arm parts. Call BuildParts() (or the editor's "Separate Into Parts").
    bool separateParts = false;   ///< serialized: rebuild the part rig on load
    bool animateParts  = true;    ///< let the built-in animation drive the parts
    /// Create the part rig as REAL child objects (a "Rig" node with one object per
    /// bone) so you can select / recolour / animate them. Safe to call any time: if a
    /// "Rig" child already exists it is ADOPTED (not duplicated) — so building it in
    /// the editor, saving, and reloading reuses the same objects instead of spawning
    /// new ones at Play. Call from the editor's "Separate Into Parts" so the parts are
    /// in the scene to customize, not spawned when you press Play.
    void BuildParts();
    /// Tear the rig back down (destroy the "Rig" child) and show the baked mesh again.
    void RemoveParts();
    /// Edit-time: ensure the rig exists and push the current/preview pose onto it, so
    /// the Animation window previews in the editor (where Update() doesn't run).
    void EditorPreviewTick();
    bool PartsBuilt() const { return m_partsBuilt; }
    GameObject* Part(int bone) const { return (bone >= 0 && bone < (int)m_parts.size()) ? m_parts[bone] : nullptr; }

    // ---- Animation authoring (the editor's Animation window uses these) ----
    std::vector<Vec3> CurrentPose() const;  ///< the pose being shown right now (per-bone euler)
    /// Force a specific per-bone pose for editor preview (overrides anim/clip until cleared).
    /// `offset` lowers/raises the whole body with it (a built-in crouch or stride
    /// preview passes StanceOffsetAt() so the feet stay on the ground).
    void PreviewPose(const std::vector<Vec3>& pose, Vec3 offset = {0, 0, 0}) {
        m_editorPose = pose; m_editorPosing = true; m_editorOffset = offset;
    }
    /// The body offset the current built-in `anim` has at clock time `t`.
    Vec3 StanceOffsetAt(float t) const { return RawStanceOffset(t); }
    void StopPreview() { m_editorPosing = false; }

    /// The first-person arm as its OWN mesh (just the arm bones), kept for the
    /// non-separated path. Rebuilt each frame while firstPersonArm.
    const Mesh& FpArmMesh() const { return m_fpArm; }
    bool        FpArmReady() const { return m_fpArmReady; }

    // ---- No-code custom clips ----
    // Set a clips file and (optionally) a clip name and it loads + plays on Start,
    // no scripting required. clipsFile is a path to a .okayanim text file.
    std::string clipsFile;
    std::string autoPlayClip;

    // ---- No-code state animations ----
    // Bind your AUTHORED clips to the movement states a controller drives (`anim`):
    // when the character enters that state it plays YOUR clip instead of the built-in
    // procedural animation. Leave a binding empty to keep the built-in for that state.
    // This is the payoff of authoring — pose+key a walk cycle, drop its name in
    // `clipWalk`, and the character walks with your animation, no scripting. Works on
    // both the single mesh and the separated part rig.
    std::string clipIdle;   ///< plays in the idle state (anim == 1)
    std::string clipWalk;   ///< plays while walking   (anim == 2)
    std::string clipRun;    ///< plays while running   (anim == 3)
    /// True while a state binding (not a manual PlayClip) is driving the current clip.
    bool StateDriven() const { return m_stateDriven; }

    /// Crossfade time (seconds) when switching clips — the character eases from its
    /// current pose into the new clip instead of snapping. So idle↔walk↔run (and any
    /// PlayClip) blend smoothly. 0 = instant. Serialized.
    float blendTime = 0.15f;
    /// True while a crossfade is in progress (0..1 weight not yet at 1).
    bool Blending() const { return m_blendT < 1.0f; }

    // ---- Animation events ----
    // Clips can carry named markers (AnimClip::events); as a clip plays past a marker
    // the character fires it. React either way: set onAnimEvent for a push callback,
    // or call ConsumeAnimEvents() each frame to drain the names that fired (footstep
    // sounds, a "hit" window on a punch, "spawn" on a throw — no scripting in the clip).
    std::function<void(const std::string&)> onAnimEvent;   // not serialized
    /// Return the event names that fired since the last call, clearing the queue.
    std::vector<std::string> ConsumeAnimEvents() { auto q = std::move(m_animEvents); m_animEvents.clear(); return q; }
    /// Pop the oldest fired event name (or "" if none) — convenient for polling one
    /// per frame from a script: `let e = anim_event(); if e == "step" { ... }`.
    std::string NextAnimEvent() {
        if (m_animEvents.empty()) return {};
        std::string s = m_animEvents.front();
        m_animEvents.erase(m_animEvents.begin());
        return s;
    }

    // Head look: layered on top of the current animation so the head turns/tilts
    // toward where the player (or camera) is aiming. Degrees; not serialized — the
    // controllers drive these as TARGETS every frame, and the head eases toward
    // them (headTurnSpeed) so it moves smoothly instead of snapping.
    float lookYaw   = 0.0f;       // + turns the head to its right
    float lookPitch = 0.0f;       // + lifts the gaze up
    float headTurnSpeed = 9.0f;   // how fast the head eases toward the look target
    float HeadYaw()   const { return m_headYaw; }
    float HeadPitch() const { return m_headPitch; }

    // Auto look-at: each frame, aim the head at the scene's main camera (or a named
    // target) with no controller or script needed — for NPCs that track the player,
    // or a character that makes eye contact with the camera. A First/Third-Person
    // controller, if attached, drives the look itself and overrides this.
    bool lookAtCamera = false;
    std::string lookAtTarget;     // object name to look at (takes priority over lookAtCamera)
    float lookHeight = 1.5f;      // head pivot height above the object's origin (aim from here)

    // Body lean: roll the upper body sideways (peeking around cover). Target in
    // degrees (+ leans to the body's right); eased like the head. Not serialized —
    // the controllers drive it each frame.
    float bodyLean = 0.0f;
    float BodyLean() const { return m_bodyLean; }

    /// Fold a desired head yaw (degrees, relative to the body) into what the neck can
    /// actually do: pass it straight through up to ~72°, then ease back toward 0 as
    /// the target swings past the neck's reach toward ±180°. This stops the head from
    /// cranking to a hard sideways pose (and "facing the camera" when the body already
    /// faces it) when the camera comes around the front. Shared by the controllers and
    /// the auto look-at so they behave consistently.
    static float NeckYaw(float relDeg) {
        while (relDeg > 180.0f)  relDeg -= 360.0f;
        while (relDeg < -180.0f) relDeg += 360.0f;
        const float lim = 72.0f;                 // comfortable neck turn
        float m = relDeg < 0.0f ? -relDeg : relDeg;
        if (m <= lim) return relDeg;
        float t = (180.0f - m) / (180.0f - lim); // 1 at the limit, 0 when fully behind
        return (relDeg < 0.0f ? -1.0f : 1.0f) * lim * t;
    }

    // ---- Manual pose: per-bone local rotation (euler deg). Applied when
    //      anim == 0. Empty / all-zero = rest pose. ----
    std::vector<Vec3> pose;

    // ---- Custom animation clips (keyframed, authored in text) ----
    /// Register a clip (replacing any with the same name).
    void AddClip(AnimClip clip);
    /// Parse `clip` blocks from text and register them; returns how many loaded.
    /// `error` is set on a parse failure. Bone tokens are the short names from
    /// BoneIndex() (e.g. "r_uparm", "torso").
    int  LoadClips(const std::string& text, std::string* error = nullptr);
    bool LoadClipsFromFile(const std::string& path, std::string* error = nullptr);
    /// Write all registered custom clips back to a .okayanim file (so in-editor
    /// keyframing can be saved to the authored format it loaded from).
    bool SaveClipsToFile(const std::string& path, std::string* error = nullptr) const;
    /// Play a registered clip by name (resets its clock). While a clip plays it
    /// drives the whole body, overriding the built-in `anim`. Returns false if no
    /// such clip. Pass "" (or call StopClip) to return to the built-in animations.
    bool PlayClip(const std::string& name);
    void StopClip();
    bool IsPlayingClip() const { return m_activeClip != nullptr; }
    /// Names of every registered clip (for editor dropdowns), sorted.
    std::vector<std::string> ClipNames() const;
    /// Is a clip with this name registered?
    bool HasClip(const std::string& name) const { return m_clips.find(name) != m_clips.end(); }
    /// How many clips are registered.
    int ClipCount() const { return (int)m_clips.size(); }
    /// Length (seconds) of a registered clip, or 0 if there's no such clip.
    float ClipDuration(const std::string& name) const {
        auto it = m_clips.find(name);
        return it == m_clips.end() ? 0.0f : it->second.Duration();
    }
    /// The active clip's playhead in seconds (0 if nothing is playing).
    float ClipTime() const { return m_activeClip ? m_clipTime : 0.0f; }
    /// The active clip's progress in [0,1] (0 if nothing is playing or zero-length).
    float ClipNormalizedTime() const {
        if (!m_activeClip) return 0.0f;
        float d = m_activeClip->Duration();
        if (d <= 0.0f) return 0.0f;
        float t = m_clipTime / d;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }
    /// True when a non-looping clip has played through to its end.
    bool ClipFinished() const {
        return m_activeClip && !m_activeClip->loop && m_clipTime >= m_activeClip->Duration();
    }
    /// The clip currently playing, or "" — handy for state checks.
    const std::string& PlayingClip() const { return m_activeClipName; }
    // ---- Animation layers (play a clip on PART of the body) ----
    // Overlay a clip on a chosen set of bones on top of whatever the body is doing —
    // e.g. wave with the upper body while the legs keep walking, or aim the arms while
    // running. The masked bones take the layer clip's pose; the rest keep the base
    // animation. Runs on its own clock and loops; call StopLayer to clear it.
    /// Play `clip` on the bones whose bit is set in `boneMask` (bit i = bone i). Returns
    /// false if there's no such clip. Pass UpperBodyMask()/ArmsMask() or build your own.
    /// `additive` ADDS the layer onto the base pose (additive layers — good for
    /// aim offsets / recoil) instead of replacing it; `weight` (0..1) blends the layer in.
    bool PlayLayer(const std::string& clip, std::uint32_t boneMask,
                   bool additive = false, float weight = 1.0f);
    void StopLayer();
    void SetLayerWeight(float w) { m_layerWeight = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w); }
    float LayerWeight() const { return m_layerWeight; }
    bool IsLayering() const { return m_layerClip != nullptr; }
    const std::string& LayerClip() const { return m_layerName; }

    /// Mirror a pose left<->right in place: swaps the L/R limb bones and flips the
    /// yaw/roll of the spine so a right-handed pose becomes its left-handed twin
    /// (Blender's symmetrize / "paste flipped"). Author one side, mirror for the other.
    static void MirrorPose(std::vector<Vec3>& pose);
    /// Handy bone masks for layering.
    static std::uint32_t ArmsMask();        ///< both upper arms, forearms, hands
    static std::uint32_t UpperBodyMask();   ///< torso, head, arms (everything above the hips)
    static std::uint32_t BoneBit(int bone) { return (bone >= 0 && bone < 32) ? (1u << bone) : 0u; }

    // ---- 1D blend tree ----
    // Blend smoothly between clips by a single parameter — the classic locomotion setup:
    // idle at speed 0, walk at ~2, run at ~5; drive the parameter from the controller's
    // speed and the body morphs between them. Clips play on a shared, looping clock so
    // their cycles stay in phase. A blend tree overrides the state/`anim` base pose; a
    // manual PlayClip still takes priority, and layers still overlay on top.
    struct BlendStop { float at; std::string clip; };
    /// Define the tree: pairs of (parameter value, clip name), any order (sorted here).
    void SetBlendTree(const std::vector<BlendStop>& stops);
    /// Set the blend parameter (e.g. the player's speed) — call each frame.
    void SetBlendParam(float v) { m_blendParam = v; }
    void ClearBlendTree() { m_blendTreeOn = false; m_blendStops.clear(); }
    bool HasBlendTree() const { return m_blendTreeOn; }
    float BlendParam() const { return m_blendParam; }

    /// Resolve a short bone token ("hips","torso","head","l_uparm","l_fore",
    /// "l_hand","r_uparm","r_fore","r_hand","l_thigh","l_shin","l_foot","r_thigh",
    /// "r_shin","r_foot") to a rig index, or -1. Case-insensitive.
    static int BoneIndex(const std::string& token);

    // ---- Rig ----
    struct Bone { int parent; Vec3 joint; };
    static int  BoneCount();
    static const char* BoneName(int i);
    std::vector<Bone> Skeleton() const;

    Mesh BuildRig(std::vector<int>& bone) const;
    Mesh Build() const { std::vector<int> b; return BuildRig(b); }

    // ---- Custom mesh rig (auto-rig an imported model) ----
    /// Bind an arbitrary mesh to this character's humanoid skeleton: each vertex
    /// attaches to its nearest bone segment, and every built-in animation, authored
    /// clip, movement state and controller then drives the model — in-editor
    /// auto-rigging. The mesh is normalized into the bind frame automatically
    /// (feet at y=0, height 1.8, centered); it should FACE +Z in its own space
    /// (the body flips 180° at display time, like the built-in mesh). Replaces
    /// the blocky body until ClearCustomBind().
    void BindCustomMesh(const Mesh& m);
    void ClearCustomBind();
    bool HasCustomBind() const { return m_hasCustomBind; }
    const Mesh& CustomBindMesh() const { return m_customBind; }

    std::vector<Vec3> PoseAt(float t) const;
    /// The pose a built-in state (an `anim` value) has at clock time t.
    std::vector<Vec3> PoseFor(int state, float t) const;
    /// World-space body offset for the current anim (e.g. lowering the whole body
    /// for crouch / prone, since the rig's root is otherwise pinned at hip height).
    Vec3 StanceOffset() const;
    void Skin(Mesh& m, const std::vector<int>& bone, const std::vector<Vec3>& rot) const;

    void Apply();
    void Start() override {
        Apply();
        if (!clipsFile.empty()) LoadClipsFromFile(clipsFile);
        if (!autoPlayClip.empty()) PlayClip(autoPlayClip);
    }
    void Update(float dt) override;

    std::string ToText() const;
    void FromText(const std::string& text);

private:
    mutable Mesh m_rest;
    mutable std::vector<int> m_bone;
    mutable std::vector<Vec3> m_restPos;
    mutable bool m_built = false;
    Mesh m_customBind;               // custom rigged mesh (bind pose, normalized)
    bool m_hasCustomBind = false;
    void AssignBonesForMesh(const Mesh& m, std::vector<int>& bone) const;
    float m_headYaw = 0.0f;        // eased head turn (toward lookYaw)
    float m_headPitch = 0.0f;      // eased head tilt (toward lookPitch)
    float m_bodyLean = 0.0f;       // eased body roll (toward bodyLean)
    int   m_prevAnim = 1;          // last frame's anim state (detects jump -> ground)
    float m_gaitSpeed = -1.0f;     // smoothed ground speed the stride is laid out for (<0 = nominal)
    float m_lowerYaw = 0.0f;       // eased turn of the hips/legs toward the travel direction (deg, ±75)
    float m_travelYaw = 0.0f;      // eased travel direction the feet step along (deg, unclamped)
    float m_gaitDir = 1.0f;        // +1 stepping forward, -1 stepping backward (backpedal)
    float m_airT = 0.0f;           // seconds off the ground (DriveLocomotion)
    Vec3  m_lastPos{0, 0, 0};      // last frame's position (measured ground speed)
    bool  m_havePos = false;
    float m_prevDt = 0.0f;         // last frame's dt (the physics step that moved the body)
    Vec3  m_measVel{0, 0, 0};      // smoothed measured horizontal velocity
    float m_gaitPhase = 0.0f;      // the stride cycle, 0..1 (0 = left touchdown); one clock for every gait state
    float m_gaitFreq = 0.0f;       // cycles/s the phase is advancing at (signed: < 0 backpedalling)
    float m_runW = 0.0f;           // 0 walking style .. 1 running style (eased over ~0.2 s)
    struct Gait;                   // stride layout for one speed (Character.cpp)
    Gait GaitFor(int state, float speed) const;
    float GaitSpeed(int state) const;                  // m_gaitSpeed, or the state's natural pace
    float NominalFreq(int state) const;                // cycles/s at a state's natural pace
    float GaitPhaseAt(float t, int state) const;       // the gait phase at clock time t
    float RunWeight(int state) const;                  // walk/run style weight for a state
    void GaitPose(std::vector<Vec3>& r, float t, int state) const;   // legs/arms/torso for walk/run/crouch-walk
    void GaitPoseFor(std::vector<Vec3>& r, const Gait& g, float phase, float hipY) const;
    float GaitHipY(float t, int state) const;          // hip drop + bounce for the same cycle
    float GaitHipYFor(const Gait& g, float phase) const;
    void  AdvanceAnimClock(float dt);                  // animTime + the gait phase at the real cadence
    float m_landT = -1.0f;         // landing-recovery progress 0..1 (<0 = idle)
    float m_landK = 0.0f;          // landing knee-bend envelope 0..1 (sin arc)
    float m_punchT = -1.0f;        // punch progress 0..1 (<0 = not punching)
    Mesh m_fpArm;                  // first-person arm-only mesh (rebuilt each frame)
    bool m_fpArmReady = false;
    void BuildFpArm(const Mesh& full);   // extract the arm bones into m_fpArm
    bool m_partsBuilt = false;
    GameObject* m_rigRoot = nullptr;
    std::vector<GameObject*> m_parts;    // one object per bone (B_COUNT)
    std::vector<Vec3> m_partRest;        // each part's joint offset from its parent
    void RememberPartRest();
    GameObject* FindRig() const;         // an existing "Rig" child, or null
    bool AdoptParts(GameObject* rig);    // reuse an existing rig instead of rebuilding
    void DriveParts();                   // pose the part transforms from the animation
    std::vector<Vec3> m_editorPose;      // forced pose for editor preview
    bool m_editorPosing = false;
    std::vector<Vec3> CurrentSourcePose() const;   // editorPose / active clip / built-in anim
    std::unordered_map<std::string, AnimClip> m_clips;  // registered custom clips
    const AnimClip* m_activeClip = nullptr;             // currently playing (or null)
    std::string m_activeClipName;
    float m_clipTime = 0.0f;
    bool m_stateDriven = false;          // a state binding (not a manual PlayClip) owns the clip
    const AnimClip* m_layerClip = nullptr;   // overlay clip (or null)
    std::string m_layerName;
    float m_layerTime = 0.0f;
    std::uint32_t m_layerMask = 0;       // which bones the layer overrides (bit i = bone i)
    bool  m_layerAdditive = false;       // add the layer onto the base instead of replacing
    float m_layerWeight = 1.0f;          // 0..1 blend of the layer
    void AdvanceLayer(float dt);         // step the layer's clock (loops)
    void ApplyLayer(std::vector<Vec3>& pose) const;   // overlay the layer onto masked bones
    std::vector<BlendStop> m_blendStops; // 1D blend tree stops, sorted by `at`
    float m_blendParam = 0.0f;           // the driving parameter (e.g. speed)
    float m_blendTreeTime = 0.0f;        // shared looping clock for the tree's clips
    bool  m_blendTreeOn = false;
    void AdvanceBlendTree(float dt);
    std::vector<Vec3> SampleBlendTree() const;   // blended pose at the current parameter
    void SyncStateClips();               // auto-play the clip bound to the current `anim` state
    std::vector<Vec3> m_blendFrom;       // pose captured at the last clip switch (crossfade source)
    float m_blendT = 1.0f;               // crossfade weight 0..1 (1 = no blend active)
    float m_blendFromY = 0.0f;           // hip height at the last switch (eased with the crossfade)
    int   m_blendFromAnim = -1;          // built-in state being crossfaded from, played live (-1 = m_blendFrom)
    Vec3  RawStanceOffset(float t) const;   // StanceOffset at clock time t, without the crossfade
    Vec3  RawStanceOffsetFor(int state, float t) const;
    Vec3  m_editorOffset{0, 0, 0};       // body offset that goes with the previewed pose
    void BeginBlend();                   // snapshot the current pose and start a crossfade
    std::vector<std::string> m_animEvents;   // event names fired since the last ConsumeAnimEvents()
    void AdvanceClip(float dt);          // step the active clip's clock and fire its events
    void FireClipEvents(float from, float to);   // emit events in the (from, to] window (handles loop wrap)
    void EnsureRest() const;
};

} // namespace okay
