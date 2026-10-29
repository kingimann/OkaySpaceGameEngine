#pragma once
// A general curve in the spline object's local space: cubic Bezier (control
// points with in / out handles) or Catmull-Rom through the points, open or
// closed, with a roll and a width / height scale per point. Evaluated by
// parameter or by distance (an arc-length table rebuilt whenever the points
// change), with rotation-minimising frames so the up vector never flips.
// SplineFollower moves an object along one; SplineMesh, SplineInstancer and
// LineRenderer build on it.
#include "okay/Scene/Component.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okay {

class GameObject;
class Scene;

/// One control point, in the spline object's local space.
struct SplinePoint {
    /// How the two tangent handles behave.
    ///   AutoSmooth - placed from the neighbours: a smooth curve through the points
    ///   Aligned    - in and out stay on one line (smooth); their lengths are free
    ///   Free       - in and out move on their own (a corner where they disagree)
    ///   Linear     - straight towards the neighbours (a third of the way): sharp corners
    enum class Mode : int { AutoSmooth = 0, Aligned = 1, Free = 2, Linear = 3 };
    Vec3  position{0, 0, 0};
    Vec3  tangentIn{0, 0, 0};    ///< handle before the point, relative to `position`
    Vec3  tangentOut{0, 0, 0};   ///< handle after the point, relative to `position`
    Mode  mode = Mode::AutoSmooth;
    float roll = 0.0f;           ///< degrees around the direction of travel; positive turns the up vector towards `right`
    Vec2  scale{1.0f, 1.0f};     ///< width (x) and height (y) of what is built along the spline here
};
// The table cache compares points byte-wise to notice edits.
static_assert(sizeof(SplinePoint) == 13 * sizeof(float), "SplinePoint must stay tightly packed");

/// Where the curve is at one place along it and which way it faces.
struct SplineSample {
    Vec3  position{0, 0, 0};
    Vec3  tangent{0, 0, 1};   ///< unit direction of travel
    Vec3  up{0, 1, 0};        ///< unit normal: rotation-minimising (or world-up) frame, with roll
    Vec3  right{1, 0, 0};     ///< Cross(up, tangent): the +X (transform.right) of an object riding the curve
    Vec2  scale{1.0f, 1.0f};
    float t = 0.0f;           ///< parameter, 0..1 over the whole spline
    float distance = 0.0f;    ///< along the curve from the first point
    /// A rider's rotation: +Z (forward) along the tangent, +Y up, +X `right`.
    Quat  Rotation() const { return Quat::LookRotation(tangent, up); }
};

class Spline : public Behaviour {
public:
    enum class Type : int { Bezier = 0, CatmullRom = 1 };
    /// How the up vector is carried along the curve. Minimal: rotation-minimising
    /// frames (no twist, never flips, follows loops and corkscrews). World: kept as
    /// close to the object's up axis as the direction allows (level roads, fences).
    enum class UpMode : int { Minimal = 0, World = 1 };

    std::vector<SplinePoint> points;
    Type   type = Type::Bezier;
    bool   closed = false;
    UpMode upMode = UpMode::Minimal;
    int    resolution = 32;   ///< arc-length table samples per segment (4..256)

    int Count() const { return (int)points.size(); }
    /// Curve pieces between points: Count() - 1 when open, Count() when closed.
    int SegmentCount() const;

    // ---- Editing (every edit is picked up by the next query) ----------------
    /// Append a point (auto-smooth handles). Returns its index.
    int AddPoint(const Vec3& localPosition);
    /// Insert `p` before `index` (clamped). Returns the index it landed at.
    int InsertPoint(int index, const SplinePoint& p);
    /// Remove a point. On a Bezier spline the neighbours' handles are lengthened
    /// so the curve stays close to its old shape (exactly undoing a SplitAt).
    bool RemovePoint(int index);
    /// Insert a point on the curve at parameter `t` without changing the shape of
    /// a Bezier spline (de Casteljau split). Returns the new point's index, or -1.
    int SplitAt(float t);
    /// Change a point's handle mode, keeping the current handles where it can.
    void SetMode(int index, SplinePoint::Mode mode);
    /// Move one handle (relative to the point). An Aligned point turns the other
    /// handle to match; `breakAlignment` makes the point Free first. Auto and
    /// Linear points take their current handles and become Aligned / Free.
    void SetHandle(int index, bool out, const Vec3& localOffset, bool breakAlignment = false);
    /// The handles the curve uses (computed ones for AutoSmooth / Linear points).
    Vec3 HandleIn(int index) const;
    Vec3 HandleOut(int index) const;
    /// Store the computed handles of AutoSmooth / Linear points in their fields.
    void ApplyAutoHandles();

    // ---- By parameter (t in 0..1 over all segments; wraps when closed) ------
    Vec3 EvaluatePosition(float t) const;   ///< world
    Vec3 EvaluateTangent(float t) const;    ///< world, unit
    Vec3 EvaluateUp(float t) const;         ///< world, unit
    Vec2 EvaluateScale(float t) const;
    SplineSample Sample(float t) const;             ///< world space, world distance
    SplineSample LocalSample(float t) const;        ///< local space, local distance

    // ---- By distance (wraps when closed, clamps when open) ------------------
    float Length() const;        ///< world units
    float LocalLength() const;   ///< in the spline object's own units
    float ParameterAtDistance(float distance) const;
    float DistanceAtParameter(float t) const;
    SplineSample SampleAtDistance(float distance) const;            ///< world
    SplineSample LocalSampleAtDistance(float localDistance) const;  ///< local
    Vec3 PositionAtDistance(float distance) const { return SampleAtDistance(distance).position; }

    // ---- Queries -------------------------------------------------------------
    /// Parameter of the curve point closest to a world position (`point` receives it).
    float NearestParameter(const Vec3& world, Vec3* point = nullptr) const;
    Vec3  NearestPoint(const Vec3& world) const;
    /// Distance along the curve of the point closest to `world`.
    float NearestDistance(const Vec3& world) const;

    /// Changes every time the curve's shape changes (points, type, closed, frames).
    std::uint64_t Revision() const;
    Mat4 LocalToWorld() const;

    /// The Spline on the object called `objectName`, or on `self`'s own object
    /// when the name is empty. Null when there is none.
    static Spline* Resolve(const Component* self, const std::string& objectName);

private:
    struct Table {
        // What the table was built from (compared on every query).
        std::vector<SplinePoint> pts;
        Type type = Type::Bezier; bool closed = false; UpMode up = UpMode::Minimal; int res = 0;
        bool valid = false;
        int segs = 0, perSeg = 0;
        std::vector<Vec3> ctrl;                 // 4 Bezier control points per segment
        std::vector<float> t, len;              // parameter, cumulative local length
        std::vector<Vec3> pos, tan, upv;        // local samples and frames (roll applied)
        std::vector<Vec2> scl;
        // World lengths for the metric of the transform the table was measured with.
        float metric[6] = {1, 0, 0, 1, 0, 1};
        bool worldValid = false;
        std::vector<float> wlen;
    };
    mutable Table m_t;
    mutable std::uint64_t m_revision = 0;

    const Table& Ensure() const;
    const std::vector<float>& WorldLengths(const Mat4& m) const;
    void Build() const;
    void Effective(const std::vector<SplinePoint>& pts, int i, Vec3& in, Vec3& out) const;
    void Controls(const std::vector<SplinePoint>& pts, int seg, Vec3 c[4]) const;
    SplineSample LocalAt(float t) const;          // exact position / tangent, table frame
    float ClampParam(float t) const;
    float WrapDistance(float d, float length) const;
    float ParamFromLengths(const std::vector<float>& lengths, float d) const;
    float LengthFromParam(const std::vector<float>& lengths, float t) const;
    SplineSample ToWorld(const SplineSample& s, const Mat4& m) const;
};

/// Moves its object along a Spline: by speed or over a fixed time, looping,
/// ping-ponging or once, optionally turned to face along the curve with the
/// spline's up. Moving platforms, rails, carts, patrols, flybys.
class SplineFollower : public Behaviour {
public:
    enum class Drive : int { Speed = 0, Time = 1 };
    enum class Wrap : int { Loop = 0, PingPong = 1, Once = 2 };

    std::string spline;          ///< the object holding the Spline
    Drive drive = Drive::Speed;
    Wrap  wrap = Wrap::Loop;
    float speed = 2.0f;          ///< units per second along the curve (negative runs backwards)
    float duration = 10.0f;      ///< seconds for one pass with Drive::Time
    float startAt = 0.0f;        ///< where it starts, as a fraction (0..1) of the length
    bool  playOnStart = true;
    bool  orient = true;         ///< turn +Z (forward) the way it travels, up from the spline's frame
    Vec3  offset{0, 0, 0};       ///< from the curve, in its frame: x along `right`, y up, z forward
    /// Called at an end of the curve (true = the far end, false = back at the start).
    std::function<void(bool atEnd)> onEnd;

    void Start() override;
    void Update(float dt) override;

    void Play() { m_playing = true; }
    void Stop() { m_playing = false; }
    bool IsPlaying() const { return m_playing; }
    /// Advance by `dt` seconds (what Update does while playing).
    void Step(float dt);
    float Distance() const { return m_distance; }
    /// Jump to a distance along the curve and place the object there.
    void SetDistance(float d);
    /// 0..1 along the curve.
    float NormalizedPosition() const;
    /// +1 forward, -1 on the way back of a ping-pong.
    int Direction() const { return m_dir; }
    Spline* Target() const;
    /// Put the object where the follower is now.
    void Place();

private:
    float m_distance = 0.0f;
    int   m_dir = 1;
    bool  m_playing = false;
    mutable GameObject* m_cached = nullptr;
    mutable std::uint64_t m_cachedVersion = 0;
    mutable std::string m_cachedName;
    void Reached(bool atEnd);
};

} // namespace okay
