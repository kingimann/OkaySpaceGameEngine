// Who owns the wheel and the use key when inventory, hotbar, camera and world all want them.
#include "test_framework.hpp"
#include <okay/Inventory/InputArbiter.hpp>

using namespace okay::InputArbiter;

int main() {
    RUN_SUITE("inventory_arbiter");
    InputWants w;
    CHECK(WheelOwner(w) == Owner::None && UseKeyOwner(w) == Owner::None);
    // closed: hotbar beats camera; world beats hotbar; modifier gives the camera back
    w.hotbarCycles = true; w.cameraZoom = true;
    CHECK(WheelOwner(w) == Owner::Hotbar);
    w.cameraModifierHeld = true; CHECK(WheelOwner(w) == Owner::Camera);
    w.cameraModifierHeld = false; w.worldWheel = true; CHECK(WheelOwner(w) == Owner::World);
    w.worldWheel = false; w.hotbarCycles = false; CHECK(WheelOwner(w) == Owner::Camera);
    // open: over the panel the inventory owns it even when everything else wants it
    InputWants o; o.inventoryOpen = true; o.hotbarCycles = o.cameraZoom = o.worldWheel = true; o.cursorOverInventory = true;
    CHECK(WheelOwner(o) == Owner::Inventory);
    o.cursorOverInventory = false; CHECK(WheelOwner(o) == Owner::Hotbar);   // world and camera are blocked
    o.hotbarCycles = false; CHECK(WheelOwner(o) == Owner::None);
    // use key
    InputWants u; u.heldItemUsable = true; CHECK(UseKeyOwner(u) == Owner::Hotbar);
    u.worldInteract = true; CHECK(UseKeyOwner(u) == Owner::World);              // a chest beats eating
    u.useKeyIsBagToggle = true; CHECK(UseKeyOwner(u) == Owner::World);          // interact beats the bag key
    u.worldInteract = false; CHECK(UseKeyOwner(u) == Owner::Inventory);         // else the bag key opens the bag
    InputWants v; v.inventoryOpen = true; v.worldInteract = true; v.heldItemUsable = true;
    CHECK(UseKeyOwner(v) == Owner::None);                                       // open UI blocks the world
    v.hoveredSlotUsable = true; CHECK(UseKeyOwner(v) == Owner::Inventory);
    v.hoveredSlotUsable = false; v.useKeyIsBagToggle = true; CHECK(UseKeyOwner(v) == Owner::Inventory);   // closes the bag
    TEST_MAIN_RESULT();
}
