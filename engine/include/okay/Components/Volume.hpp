#pragma once
// Volumes: one shared region shape (box, sphere or cylinder) and the zone behaviours that use it.
// The shape math (VolumeGeometry) is plain data + functions so the editor draws, resizes and tests
// the very same region the game runs. See docs/volumes.md.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace okay {

class Scene;

enum class VolumeShape { Box = 0, Sphere = 1, Cylinder = 2 };
constexpr int kVolumeShapeCount = 3;

/// The region itself: a shape that fills the box `size` (local units, scaled by the object's Transform)
/// centred at `offset` (local units). A sphere is the ellipsoid inscribed in that box and a cylinder is the
/// elliptic cylinder standing on the local Y axis, so one `size` drives every shape.
struct VolumeGeometry {
    VolumeShape shape = VolumeShape::Box;
    Vec3 size{4.0f, 3.0f, 4.0f};
    Vec3 offset{0.0f, 0.0f, 0.0f};

    static constexpr float kMinSize = 0.05f;   ///< the smallest a handle drag leaves an axis

    static const char* ShapeName(VolumeShape s);
    /// A size that can hold something: every axis finite and above zero.
    bool Valid() const;

    /// `world` in the region's own units: (0,0,0) at the centre, +-1 on the faces of the box.
    /// An axis with no size gives +infinity, so nothing is ever inside.
    Vec3 ToUnit(const Transform& t, const Vec3& world) const;
    /// How deep a unit-space point is inside: 1 at the centre, 0 on the surface, negative outside.
    float Depth(const Vec3& unit) const;
    bool Contains(const Transform& t, const Vec3& world) const { return Depth(ToUnit(t, world)) >= 0.0f; }
    float DepthAt(const Transform& t, const Vec3& world) const { return Depth(ToUnit(t, world)); }

    Vec3 WorldCenter(const Transform& t) const { return t.TransformPoint(offset); }
    /// The point at the middle of the region's floor, in world space.
    Vec3 WorldFloorCenter(const Transform& t) const { return t.TransformPoint({offset.x, offset.y - size.y * 0.5f, offset.z}); }

    /// Move one face of the box: `axis` 0..2, `sign` -1 or +1 for the face on the minus or plus side,
    /// `delta` how far the face moves along +axis in local units (negative pulls the plus face in or
    /// pushes the minus face out). The opposite face stays where it is. Returns the delta really applied
    /// (a size is never left below kMinSize).
    float DragFace(int axis, int sign, float delta);

    /// The region a ground drag from `a` to `b` draws: the rectangle between them on the floor, `height` tall.
    /// Gives the world position of the new object and its size.
    static void FromGroundDrag(const Vec3& a, const Vec3& b, float height, Vec3& outPosition, Vec3& outSize);
};

/// One region plus what it does. Pick a `kind`:
///  - Checkpoint: a body inside it makes the middle of its floor that body's respawn point.
///  - Kill: a body that enters is hurt (`killDamage`) and then sent back to its last checkpoint, or to where it
///    began; with Destroy it is removed instead.
///  - Force: pushes the bodies inside it each physics step: a steady direction (wind, a draught, an updraft) or
///    away from / toward the centre.
///  - Atmosphere: while the camera is inside, the scene's fog and vignette fade to this volume's, and fade back
///    when it leaves.
///  - Ambience: plays the AudioSource on the same object while the camera is inside, fading in and out.
///  - Reverb: while the listener is inside, 3D sounds gain this room's echo (room, hall, cave or outdoor),
///    blending in across `blend` of the way from the edge. 2D sounds stay dry. Nothing echoes until a
///    Reverb volume exists.
/// A body is an object with a Rigidbody 3D, or any object whose tag (or name text) matches `activatorTag`.
/// No collider is needed: the volume tests the shape itself.
class Volume : public Behaviour {
public:
    enum class Kind { Checkpoint = 0, Kill = 1, Force = 2, Atmosphere = 3, Ambience = 4, Reverb = 5 };
    static constexpr int kKindCount = 6;
    enum class KillAction { Respawn = 0, Destroy = 1 };
    enum class ForceShape { Direction = 0, Radial = 1 };

    // ---- The region ----
    Kind  kind = Kind::Checkpoint;
    VolumeShape shape = VolumeShape::Box;
    Vec3  size{4.0f, 3.0f, 4.0f};      ///< box the shape fills, in local units
    Vec3  offset{0.0f, 0.0f, 0.0f};    ///< centre, in local units
    std::string activatorTag = "Player";   ///< who counts (tag or name text). Blank: any body (Force)
    std::string eventName;             ///< Checkpoint / Kill: sent to scripts as an On Logic Signal when it fires

    // ---- Kill ----
    int   killAction = (int)KillAction::Respawn;
    float killDamage = 0.0f;           ///< health taken before the respawn (0 = none)

    // ---- Force ----
    int   forceShape = (int)ForceShape::Direction;
    Vec3  forceDirection{0.0f, 1.0f, 0.0f};
    bool  forceLocal = true;           ///< Direction: turns with the object (off: world axes)
    float forceStrength = 12.0f;       ///< acceleration in m/s^2; Radial: positive pushes out, negative pulls in

    // ---- Atmosphere ----
    Color fogColor = Color::FromBytes(150, 170, 190);
    float fogStart = 2.0f;
    float fogEnd   = 30.0f;
    float vignette = 0.25f;
    float blend    = 0.35f;            ///< how far in from the edge it takes to reach full strength (0 = at once), 0..1 of the half size

    // ---- Ambience ----
    float fadeTime = 1.5f;             ///< seconds to fade the sound in and out

    // ---- Reverb ----
    int   reverbPreset = 0;            ///< 0 room, 1 hall, 2 cave, 3 outdoor
    float reverbAmount = 1.0f;         ///< scales the preset's echo, 0 = dry, 1 = the preset

    // ---- Read-outs ----
    VolumeGeometry Geometry() const { return {shape, size, offset}; }
    bool Contains(const Vec3& world) const;
    /// Bodies inside it right now (as of the last update).
    int Occupants() const { return (int)m_inside.size(); }
    /// The camera is inside (Atmosphere / Ambience / Reverb) and how strongly, 0..1.
    float ViewerWeight() const { return m_weight; }
    /// 0 outside, 1 at full strength. `blend` is how far in from the edge (0..1 of
    /// the depth) it takes to reach 1. The same fade Atmosphere and Reverb use.
    float WeightAt(const Vec3& world) const;
    /// Where this body respawns: false when nothing is known about it yet.
    static bool RespawnPoint(const GameObject* body, Vec3& out);
    /// Set a body's respawn point by hand (scripts, tests).
    static void SetRespawnPoint(const GameObject* body, const Vec3& where);

    static const char* KindName(Kind k);
    static const char* KindHint(Kind k);
    /// The colour the editor draws a kind in.
    static Color KindColor(Kind k);

    void Start() override;
    void Update(float dt) override;
    void FixedUpdate(float step) override;
    void OnDisable() override;
    void OnDestroy() override;

private:
    std::unordered_set<std::uint64_t> m_inside;   // bodies inside last frame (InstanceId)
    float m_weight = 0.0f;
    bool  m_viewerWasInside = false;
    bool  m_atmoEngaged = false;                  // this volume is the one holding the scene's fog settings
    struct AtmoBase { bool fog = false; Color color; float start = 0, end = 0, vignette = 0; int mode = 0; };
    AtmoBase m_base;                              // the scene's own settings, kept while engaged

    void Bodies(std::vector<GameObject*>& out) const;
    bool Wanted(GameObject* g) const;
    void UpdateBodies();
    void UpdateAtmosphere();
    void UpdateAmbience();
    void Respawn(GameObject* body);
    bool ViewerPosition(Vec3& out) const;
    void ReleaseAtmosphere();
};

} // namespace okay
