#pragma once
// Who owns the mouse wheel and the use key on one frame when the open inventory, the hotbar,
// the camera and the world all want them. Pure: the caller fills `InputWants` from its own
// state and asks, so the rule is tested without a window. Written from the plain rules below.
//
// Mouse wheel:
//   inventory open, cursor over it   -> Inventory (it scrolls, or swallows the wheel)
//   inventory open, cursor elsewhere -> Hotbar if it cycles, else nobody (the UI blocks the world)
//   inventory closed: camera modifier held and zoom wanted -> Camera; world wants it -> World;
//                     hotbar cycles -> Hotbar; zoom wanted -> Camera; else nobody
// Use key:
//   inventory open   -> Inventory when a slot under the cursor is usable, or the key is the bag
//                       key (it closes the bag), else nobody
//   inventory closed -> World when something can be interacted with, else the bag toggle when
//                       the use key is also the bag key, else Hotbar when the held item is usable.
namespace okay {
namespace InputArbiter {

enum class Owner { None, Inventory, Hotbar, World, Camera };

struct InputWants {
    bool inventoryOpen = false;
    bool cursorOverInventory = false;
    bool hotbarCycles = false;          ///< wheel changes the selected hotbar slot
    bool worldWheel = false;            ///< something in the world wants the wheel (e.g. adjusting a tool)
    bool cameraZoom = false;            ///< the camera wants the wheel (zoom)
    bool cameraModifierHeld = false;    ///< e.g. Ctrl: the player asked for the camera
    bool hoveredSlotUsable = false;     ///< open inventory: the slot under the cursor can be used
    bool worldInteract = false;         ///< a chest, bench or pickup wants the use key
    bool useKeyIsBagToggle = false;     ///< the use key is also the key that opens/closes the bag
    bool heldItemUsable = false;        ///< the selected hotbar item can be used
};

inline Owner WheelOwner(const InputWants& w) {
    if (w.inventoryOpen) {
        if (w.cursorOverInventory) return Owner::Inventory;
        return w.hotbarCycles ? Owner::Hotbar : Owner::None;
    }
    if (w.cameraModifierHeld && w.cameraZoom) return Owner::Camera;
    if (w.worldWheel) return Owner::World;
    if (w.hotbarCycles) return Owner::Hotbar;
    if (w.cameraZoom) return Owner::Camera;
    return Owner::None;
}

inline Owner UseKeyOwner(const InputWants& w) {
    if (w.inventoryOpen) return (w.hoveredSlotUsable || w.useKeyIsBagToggle) ? Owner::Inventory : Owner::None;
    if (w.worldInteract) return Owner::World;
    if (w.useKeyIsBagToggle) return Owner::Inventory;
    return w.heldItemUsable ? Owner::Hotbar : Owner::None;
}

} // namespace InputArbiter
} // namespace okay
