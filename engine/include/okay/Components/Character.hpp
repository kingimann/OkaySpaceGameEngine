#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Components/AnimClip.hpp"
#include <unordered_map>
#include <vector>
#include <string>

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
                             // 13 cheer/happy,14 sad,15 angry,16 think (gestures + emotions)
    float animSpeed = 1.0f;
    float animTime  = 0.0f;  // runtime clock (not serialized)

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

    // ---- Separate body parts (a real, editable rig) ----
    // Instead of one baked mesh, build the character as a HIERARCHY of part
    // GameObjects (one per bone: Hips, Torso, Head, arms, legs) you can select,
    // recolour, attach things to (a sword in the Hand), and animate part-by-part.
    // The built-in animations still play (they drive the part transforms) unless you
    // turn `animateParts` off to author your own. The first-person hand just hides
    // the non-arm parts. Call BuildParts() (or the editor's "Separate Into Parts").
    bool separateParts = false;   ///< serialized: rebuild the part rig on load
    bool animateParts  = true;    ///< let the built-in animation drive the parts
    void BuildParts();
    bool PartsBuilt() const { return m_partsBuilt; }
    GameObject* Part(int bone) const { return (bone >= 0 && bone < (int)m_parts.size()) ? m_parts[bone] : nullptr; }

    /// The first-person arm as its OWN mesh (just the arm bones), kept for the
    /// non-separated path. Rebuilt each frame while firstPersonArm.
    const Mesh& FpArmMesh() const { return m_fpArm; }
    bool        FpArmReady() const { return m_fpArmReady; }

    // ---- No-code custom clips ----
    // Set a clips file and (optionally) a clip name and it loads + plays on Start,
    // no scripting required. clipsFile is a path to a .okayanim text file.
    std::string clipsFile;
    std::string autoPlayClip;

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
    /// Play a registered clip by name (resets its clock). While a clip plays it
    /// drives the whole body, overriding the built-in `anim`. Returns false if no
    /// such clip. Pass "" (or call StopClip) to return to the built-in animations.
    bool PlayClip(const std::string& name);
    void StopClip();
    bool IsPlayingClip() const { return m_activeClip != nullptr; }
    /// The clip currently playing, or "" — handy for state checks.
    const std::string& PlayingClip() const { return m_activeClipName; }
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

    std::vector<Vec3> PoseAt(float t) const;
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
    float m_headYaw = 0.0f;        // eased head turn (toward lookYaw)
    float m_headPitch = 0.0f;      // eased head tilt (toward lookPitch)
    float m_bodyLean = 0.0f;       // eased body roll (toward bodyLean)
    float m_punchT = -1.0f;        // punch progress 0..1 (<0 = not punching)
    Mesh m_fpArm;                  // first-person arm-only mesh (rebuilt each frame)
    bool m_fpArmReady = false;
    void BuildFpArm(const Mesh& full);   // extract the arm bones into m_fpArm
    bool m_partsBuilt = false;
    GameObject* m_rigRoot = nullptr;
    std::vector<GameObject*> m_parts;    // one object per bone (B_COUNT)
    void DriveParts();                   // pose the part transforms from the animation
    std::unordered_map<std::string, AnimClip> m_clips;  // registered custom clips
    const AnimClip* m_activeClip = nullptr;             // currently playing (or null)
    std::string m_activeClipName;
    float m_clipTime = 0.0f;
    void EnsureRest() const;
};

} // namespace okay
