#pragma once
#include "okay/Scene/Component.hpp"
#include <string>

namespace okay {

class Scene;
class GameObject;

/// One press, one owner. Queried by inventory, lean, flashlight, and build code
/// so call order does not decide who gets the key. BestInteract is cached for
/// the frame (Scene::Update calls BeginFrame).
struct InteractChoice {
    enum class Kind { None, Container, Pickup, Logic };
    Kind kind = Kind::None;
    Behaviour* target = nullptr;
    std::string prompt;    ///< "E — Open Chest", empty when there is nothing to use
};

enum class WheelOwner { None, Gameplay, Hotbar, Bag };

class InputArbiter {
public:
    static void BeginFrame(const Scene* s);

    static InteractChoice BestInteract(const Scene* s);
    static char SharedInteractKey(const Scene* s);
    static int  SharedInteractPad(const Scene* s);
    /// True on the frame the shared interact key or its gamepad button goes down.
    static bool SharedInteractDown(const Scene* s);

    /// A chest / bench whose own key is `key` is in range.
    static bool KeyOwnedByWorld(const Scene* s, char key);
    /// Lean should ignore `key` this frame (a world use owns it, or it is the
    /// backpack key on the frame it opens).
    static bool SuppressLean(const Scene* s, char key);

    static bool BagOpen(const Scene* s);
    static bool HotbarOwnsNumberKeys(const Scene* s);
    static bool GridDragOwnsRotate(const Scene* s, char key);
    static WheelOwner WheelFor(const Scene* s);

    /// Keys bound on more than one component of `go` (empty when they don't clash).
    static std::string KeyConflictSummary(const GameObject* go);
};

} // namespace okay
