#pragma once
// ---------------------------------------------------------------------------
// PlayerContract: the one place that says in what ORDER the parts of a player
// run inside a frame, and WHO gets each shared input. Plain constants and
// pure functions; nothing here owns state. tests/test_integration_player.cpp
// asserts that every component's real ExecutionOrder() agrees with this table.
//
// A frame: Update runs components by ExecutionOrder (low first, creation order
// among equals), then LateUpdate runs in the same order.
//   Update     input + movement -> use key (pickup, seat) -> animation -> IK -> view
//   LateUpdate camera placement (reads the position the controller just wrote)
// ---------------------------------------------------------------------------
namespace okay {
namespace PlayerContract {

namespace Order {
constexpr int Behaviour   = -20;   ///< AI: behaviour trees, grid obstacles (decide before bodies move)
constexpr int Controller  = -10;   ///< player controllers (first / third person, character 2D/3D, top-down, nav agent)
constexpr int Interaction = 0;     ///< use key: item pickup, vehicle seat, chest (default order: reads what the controller did)
constexpr int Animation   = 0;     ///< animators: after the controllers, before every IK pass
constexpr int FootIK      = 85;
constexpr int Holding     = 90;
constexpr int View        = 95;    ///< Player View blend
constexpr int FullBodyIK  = 100;
constexpr int LimbIK      = 105;
constexpr int AimIK       = 110;   ///< last body solver: aims from the final pose
constexpr int Cutscene    = 120;
constexpr int Skinning    = 200;   ///< skinned mesh, captions: after everything that moves bones
}

/// Who may take a shared input on one frame. One answer, so two systems never both act on a key.
enum class Owner { None, Menu, Inventory, Vehicle, World, Controller, Camera };

struct Situation {
    bool paused = false;             ///< the game is paused (Game::Paused())
    bool uiOpen = false;             ///< a menu or the open bag has the pointer
    bool seated = false;             ///< the player sits in a vehicle seat
    bool vehicleSeatInRange = false; ///< a free driver seat is within its use radius
    bool pickupInRange = false;      ///< an item with a use key is within range
    bool zoomWanted = false;         ///< the camera wants the wheel (orbit zoom)
    bool hotbarCycles = false;       ///< the wheel changes the hotbar slot
};

/// The use key (default E; it is also lean-right in the controllers, which lean only when
/// nobody else took it). Seated: the seat (to get out). Otherwise a pickup in range beats a
/// car, else a car, else the lean.
inline Owner UseKeyOwner(const Situation& s) {
    if (s.paused) return Owner::None;
    if (s.uiOpen) return Owner::Menu;
    if (s.seated) return Owner::Vehicle;
    if (s.pickupInRange) return Owner::World;
    if (s.vehicleSeatInRange) return Owner::Vehicle;
    return Owner::Controller;
}

/// The mouse wheel: a menu first, then the hotbar, then camera zoom.
inline Owner WheelOwner(const Situation& s) {
    if (s.paused) return Owner::None;
    if (s.uiOpen) return Owner::Menu;
    if (s.hotbarCycles) return Owner::Inventory;
    if (s.zoomWanted) return Owner::Camera;
    return Owner::None;
}

/// Movement keys: the car while seated, else the controller; nobody while paused or in a menu.
inline Owner MoveKeysOwner(const Situation& s) {
    if (s.paused || s.uiOpen) return Owner::None;
    return s.seated ? Owner::Vehicle : Owner::Controller;
}

/// Mouse look: the camera; nobody while paused or a menu has the pointer.
inline Owner MouseLookOwner(const Situation& s) {
    if (s.paused || s.uiOpen) return Owner::None;
    return Owner::Camera;
}

} // namespace PlayerContract
} // namespace okay
