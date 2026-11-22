#pragma once
// Wind (docs/gameplay/wind.md): the Wind Zone component and the things that listen to the wind.
//
//  - WindZone:  blows. "Everywhere" is the scene's own wind; "Direction" and "Pushes outward" are regions
//               (box, sphere, cylinder) that blow inside them. The maths lives in Environment/WindField.hpp.
//  - WindDrag:  a Rigidbody 3D (or a ragdoll's bones) is pushed by the wind, like a leaf or a paper box.
//  - WindSway:  a mesh (tree, bush, grass patch, flag) leans and shivers in the wind.
//  - WindAmbience: the sound of wind whose loudness follows how hard it blows, made by us (no sound file).
// Written from general ideas only: the drag equation F = 1/2 * air density * Cd * area * v^2 (any physics text),
// a bend that grows with height, a travelling wave for a flag, and a filtered-noise whoosh.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Environment/WindField.hpp"
#include "okay/Audio/AudioClip.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

class Scene;

class WindZone : public Behaviour {
public:
    enum class Mode { Everywhere = 0, Direction = 1, Outward = 2 };

    Mode  mode = Mode::Direction;
    // ---- Region (not used by Everywhere) ----
    int   shape = 0;                   ///< 0 box, 1 sphere, 2 cylinder (standing on the local Y axis)
    Vec3  size{10.0f, 6.0f, 10.0f};    ///< the box the shape fills, in local units (the Transform's scale multiplies it)
    Vec3  offset{0, 0, 0};             ///< where its centre sits, in local units
    float edge = 0.3f;                 ///< 0..1: how far in from the surface it takes to reach full strength
    float falloff = 0.0f;              ///< 0..1: how much weaker the wind gets toward the surface
    // ---- The wind ----
    Vec3  direction{0, 0, 1};          ///< the way it blows, in local units: turn the object to aim it
    float strength = 5.0f;             ///< metres per second (about 3 = a breeze, 10 = a gale)
    float gustAmount = 0.0f;           ///< 0..1: how much the strength swings; 1 = from calm up to double
    float gustFrequency = 0.2f;        ///< gusts per second (0.2 = one every 5 seconds)
    float gustSpeed = 10.0f;           ///< how fast a gust travels downwind (m/s); 0 = everywhere at once
    float turbulence = 0.0f;           ///< 0..1: random swirl as a fraction of the strength
    float turbulenceScale = 0.15f;     ///< smaller = bigger swirls
    int   seed = 1;                    ///< same seed, same wind
    std::string curveKeys;             ///< optional strength over time: "seconds:multiplier" pairs, e.g. "0:0.2, 10:1.5, 20:0.2"
    float curveLength = 20.0f;         ///< seconds before the curve repeats
    // ---- Blending ----
    int   priority = 0;                ///< where winds overlap the higher number is applied last
    bool  additive = false;            ///< add to the wind underneath instead of replacing it inside the zone

    /// This zone as plain numbers in world space. Never fails: a broken zone gives a source that covers nothing.
    WindSource Snapshot() const;
    /// The wind this zone alone makes at a world position, now. (0,0,0) outside it.
    Vec3 WindAt(const Vec3& world) const;
    /// The way it blows in the world, unit length (0,0,0 for a zero direction).
    Vec3 WorldDirection() const;
    /// Is the object's centre inside the region (Everywhere: always)?
    bool Covers(const Vec3& world) const { WindSource s = Snapshot(); return s.Coverage(world) > 0.0f; }

    /// Parse "seconds:multiplier, ..." (bad pieces are skipped) / write it back.
    static std::vector<std::pair<float, float>> ParseCurve(const std::string& text);
    static std::string FormatCurve(const std::vector<std::pair<float, float>>& keys);

    /// The scene's own wind: the first live "Everywhere" zone (nullptr when there is none).
    static WindZone* FindGlobal(Scene& scene);
    /// The scene's own wind, made (an object called "Wind" with an Everywhere zone) when the scene has none.
    static WindZone* EnsureGlobal(Scene& scene);
    /// Aim an Everywhere / Direction zone along a world direction (turns the object). A zero or bad direction changes nothing.
    void Aim(const Vec3& worldDirection);

    static const char* ModeName(Mode m);
    static const char* ModeHint(Mode m);

    // ---- The scene's wind, once a frame ----
    /// Gather every live zone into the shared field (WindField::Publish) and move the wind clock on by `dt`.
    /// The Scene calls it every update right after the weather.
    static void UpdateAll(float dt);
    /// Live zones (for the editor and tests).
    static const std::vector<WindZone*>& Live();

    void OnEnable() override;
    void OnDisable() override;
    ~WindZone() override;
};

/// A body that the wind pushes. The push is the drag equation on the wind's speed relative to the body, so a
/// light body with a big area is blown about and a heavy small one barely moves. Off unless this component exists.
class WindDrag : public Behaviour {
public:
    float area = 1.0f;                 ///< the size it presents to the wind, in square metres
    float dragCoefficient = 1.0f;      ///< 0.5 = streamlined, 1 = a flat box, 2 = a parachute
    float strength = 1.0f;             ///< 0..10: a simple volume knob for the whole push
    bool  includeChildren = false;     ///< push every Rigidbody 3D below this object too (a ragdoll's bones)

    /// The force the wind puts on a body (Newtons). `relative` = the wind minus the body's own velocity.
    /// The push is capped so one physics step never overshoots the wind speed (no jitter on a light body).
    static Vec3 Force(const Vec3& relative, float area, float cd, float mass, float step);

    void FixedUpdate(float step) override;
    static constexpr float kAirDensity = 1.2f;   ///< kg per cubic metre
};

/// A mesh that leans and shivers. Put it on an object with a Mesh Renderer.
class WindSway : public Behaviour {
public:
    enum class Weight { Height = 0, Flag = 1, Custom = 2 };
    Weight weight = Weight::Height;
    float amount = 0.4f;               ///< metres the top moves at a strong wind (12 m/s)
    float height = 0.0f;               ///< metres of the mesh that bend (0 = measure it)
    float stiffness = 2.0f;            ///< 1 = bends evenly up the height, 3 = only the top bends
    float flutter = 0.3f;              ///< 0..1: leaves shiver
    float flutterSpeed = 3.0f;         ///< shivers per second
    float updateRate = 20.0f;          ///< times a second the mesh is redone
    float maxDistance = 80.0f;         ///< not redone when the camera is further than this (0 = always)
    bool  turnToWind = true;           ///< Flag: swing the object around so it points downwind

    /// Weights of a Custom mesh: one 0..1 number per vertex (a Foliage chunk sets them from the plants' heights).
    void SetWeights(std::vector<float> w) { m_custom = std::move(w); m_hasBase = false; }

    /// How far a vertex at weight `w` moves in a wind of `speed` (metres, along the wind), `stiff` as `stiffness`.
    static float Bend(float w, float speed, float amount, float stiff);

    void Update(float dt) override;
    void OnDisable() override;
    /// Put the mesh back the way it was drawn at first.
    void Restore();
    /// Do one update now whatever the timer says (tests, and the first frame).
    void Step(float dt);
    bool Active() const { return m_hasBase; }

private:
    std::vector<Vec3> m_base;
    std::vector<float> m_custom;
    std::vector<float> m_w;
    bool  m_hasBase = false;
    float m_minY = 0, m_maxY = 1, m_minX = 0, m_maxX = 1;
    Vec3  m_center{0, 0, 0};
    float m_timer = 0.0f, m_time = 0.0f;
    Vec3  m_lastFront{0, 0, 0}, m_lastBack{0, 0, 0};   // what was written last: a different value means the mesh was replaced
    void Capture();
};

/// The sound of wind. Adds a looping sound (made here, no file) to this object and turns it up and down with the
/// wind at the listener. A plain 2D sound: it is the air around you, not a place.
class WindAmbience : public Behaviour {
public:
    float volume = 0.6f;               ///< loudest it gets (0..1)
    float fullAt = 12.0f;              ///< wind speed that is the loudest (m/s)
    float quietBelow = 0.3f;           ///< silent in a wind slower than this (m/s)
    float smoothing = 1.5f;            ///< seconds it takes to follow a change (gusts swell, they do not click)

    /// A seamlessly looping whoosh: low-pass filtered noise with slow swells. Same seed, same samples.
    static AudioClip MakeClip(float seconds = 6.0f, std::uint32_t seed = 1, int rate = 22050);
    /// Loudness 0..1 for a wind speed.
    static float Loudness(float speed, float quietBelow, float fullAt);
    float Current() const { return m_level; }

    void Start() override;
    void Update(float dt) override;
private:
    float m_level = 0.0f;
};

} // namespace okay
