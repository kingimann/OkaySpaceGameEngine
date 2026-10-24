#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/Buildable.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

class LogicTarget;

// ---------------------------------------------------------------------------
// Logic links: players wire placed objects together without scripting. A
// LogicSource (button, pressure plate, trigger zone, timer) has an on/off output;
// each link sends it to a LogicTarget (door, mover, light, spawner, event) on
// another object. Links name targets by Buildable id, so they survive save/load
// and undo. Both are plain components, so a prefab is "a button" or "a door"
// just by carrying one, and every setting is data in the prefab.
// ---------------------------------------------------------------------------

/// The input end of a logic link: produces an on/off signal and sends it down
/// every link each frame.
///  - Button: press it (aim + interact key, or `Press()` from code). Toggle
///    buttons flip on/off; momentary ones stay on for `holdTime` seconds.
///  - PressurePlate / TriggerZone: on while something is inside the zone box
///    (`zoneSize` at `zoneOffset`, in the object's local units). A plate's zone
///    sits on top of it; a trigger zone's fills the object. Anything that moves
///    counts (players, physics props); static level geometry and placed blocks
///    without a Rigidbody 3D don't. `activatorTag` narrows it to one tag.
///  - Timer: on for `onTime` seconds out of every `interval`.
class LogicSource : public Behaviour {
public:
    enum class Kind { Button = 0, PressurePlate = 1, TriggerZone = 2, Timer = 3 };
    static constexpr int kKindCount = 4;

    Kind  kind     = Kind::Button;
    bool  toggle   = true;                    ///< Button: each press flips (off = momentary)
    float holdTime = 1.0f;                    ///< Button (momentary): seconds on after a press
    float interval = 2.0f;                    ///< Timer: seconds per cycle
    float onTime   = 0.5f;                    ///< Timer: seconds of each cycle it's on
    bool  startOn  = false;                   ///< Button (toggle) / Timer: start switched on
    Vec3  zoneSize{0.9f, 0.6f, 0.9f};         ///< Plate / zone: detection box (local units)
    Vec3  zoneOffset{0.0f, 0.35f, 0.0f};      ///< Plate / zone: box centre (local units)
    std::string activatorTag;                 ///< Plate / zone: only this tag counts (blank = anything that moves)
    char  pressKey = 'e';                     ///< Button: interact key while aiming at it (0 = off)
    int   pressPadButton = 2;                 ///< Button: gamepad button (2 = X; -1 = off)
    float pressReach = 4.0f;                  ///< Button: how far away it can be pressed
    std::vector<int> links;                   ///< Buildable ids of the targets it drives

    /// Current output.
    bool Output() const { return m_out; }
    /// Press the button (toggle flips; momentary turns on for holdTime).
    void Press();
    /// Force the output (scripts). Plates / zones recompute it next frame.
    void SetOutput(bool on);
    /// Things in the plate / zone box last frame.
    int Occupants() const { return m_occupants; }

    bool HasLink(int targetId) const;
    bool AddLink(int targetId);      ///< false if already linked
    bool RemoveLink(int targetId);   ///< false if it wasn't linked

    /// The targets the links currently reach (resolved by id).
    std::vector<LogicTarget*> Targets() const;

    static const char* KindName(Kind k);

    void Update(float dt) override;

private:
    bool  m_out = false;
    bool  m_init = false;
    float m_timer = 0.0f;
    int   m_occupants = 0;
    // Link targets resolved by id, refreshed when the scene's objects change.
    mutable std::vector<LogicTarget*> m_cache;
    mutable std::uint64_t m_cacheVersion = 0;
    mutable bool m_cacheDirty = true;

    int  CountOccupants() const;
    bool AimedAt() const;
};

/// The output end of a logic link: reacts to the OR of every source linked to it.
///  - Door: slides by `openOffset` and turns by `openYaw` (in its own rotated
///    frame) over `moveTime` seconds; closes again when the signal goes off.
///  - Mover: like a door but travels back and forth while the signal is on.
///  - Light: switches every Light under it (to `lightIntensity`) and lights its
///    meshes with `onEmissive`.
///  - Spawner: spawns `spawnPrefab` at `spawnOffset` each time it's switched on
///    (or calls a Spawner component on the object when no prefab is set).
///  - Event: sends the custom event `eventName` (value 1 on / 0 off) to every
///    visual script, script and Actions list in the scene.
/// Mode Follow = on while the signal is on; Toggle = each new signal flips it.
/// `invert` swaps on and off. Every change also raises On Logic Signal.
class LogicTarget : public Behaviour {
public:
    enum class Kind { Door = 0, Mover = 1, Light = 2, Spawner = 3, Event = 4 };
    static constexpr int kKindCount = 5;
    enum class Mode { Follow = 0, Toggle = 1 };

    Kind  kind   = Kind::Door;
    Mode  mode   = Mode::Follow;
    bool  invert = false;
    Vec3  openOffset{0.0f, 2.0f, 0.0f};       ///< Door / mover: how far it travels (its rotated frame)
    float openYaw  = 0.0f;                    ///< Door / mover: degrees it turns about its up axis
    float moveTime = 0.6f;                    ///< Door / mover: seconds for one trip
    float lightIntensity = 1.0f;              ///< Light: intensity when on
    Color onEmissive = Color::FromBytes(255, 214, 120);   ///< Light: mesh glow when on
    std::string spawnPrefab;                  ///< Spawner: .okayprefab to spawn
    Vec3  spawnOffset{0.0f, 1.5f, 0.0f};      ///< Spawner: where, from the object (its rotated frame)
    int   maxAlive = 5;                       ///< Spawner: cap on its live spawns (0 = no cap)
    std::string eventName;                    ///< Event: custom event to send (also the signal name)

    /// Feed one source's output for this frame (sources call it from Update).
    void Feed(bool on) { m_fed = m_fed || on; }
    /// Any source on last frame?
    bool Powered() const { return m_powered; }
    /// Current on/off state (after mode and invert).
    bool State() const { return m_state; }
    /// Force the state (scripts). The next signal change takes over again.
    void SetState(bool on);
    /// Door / mover: 0 = closed .. 1 = open.
    float Amount() const { return m_t; }
    /// Door / mover: the closed pose (false until it has moved once).
    bool ClosedPose(Vec3& pos, Quat& rot) const;
    /// Door / mover: jump back to the closed pose (build mode does this before moving it).
    void ResetPose();
    /// Door / mover: hold still in the closed pose (build mode, while a player carries it).
    void SetSuspended(bool suspended) { if (suspended) ResetPose(); m_suspended = suspended; }
    /// The name On Logic Signal reports: `eventName`, else the object's name.
    std::string SignalName() const;
    /// Spawn one now (Spawner kind). Returns the new object or nullptr.
    GameObject* SpawnNow();

    static const char* KindName(Kind k);

    void LateUpdate(float dt) override;

private:
    bool  m_fed = false, m_powered = false, m_state = false, m_prevAct = false, m_init = false;
    float m_t = 0.0f;              // door / mover travel 0..1
    int   m_dir = 1;               // mover direction
    bool  m_hasPose = false, m_suspended = false;
    Vec3  m_closedPos;
    Quat  m_closedRot;
    std::vector<std::uint64_t> m_spawned;   // live spawns, for maxAlive
    void Changed();
    void ApplyLight();
};

/// Raise On Logic Signal across a scene: every visual script's On Logic Signal
/// node (Other = `who`, Event Name = `name`, Event Value = `on`) and every
/// script's on_logic_signal(name) (globals `signal_name`, `signal_on`). With
/// `customEvent` set it is also delivered as that custom event.
void BroadcastLogicSignal(Scene& scene, GameObject* who, const std::string& name, bool on,
                          const std::string& customEvent = "");

} // namespace okay
