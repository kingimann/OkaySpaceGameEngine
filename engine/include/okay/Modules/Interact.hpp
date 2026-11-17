#pragma once
#include "okay/Math/Vec3.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay {

class GameObject;
class Scene;

/// Who is interacting and how.
struct InteractContext {
    GameObject* actor = nullptr;   ///< the player (or an NPC) pressing the key
    bool hold = false;             ///< the key is being held
    float heldSeconds = 0.0f;
    bool inspect = false;          ///< look at it instead of using it
};

/// Anything an actor can use by walking up and pressing the interact key: a pickup, a chest, a person to
/// talk to, a seat. The components keep their own behaviour and settings (old scenes load unchanged); a module
/// registers an adapter for each of its components (engine/modules/<name>/register.cpp).
class IInteractable {
public:
    virtual ~IInteractable() = default;
    virtual GameObject* Object() const = 0;
    /// Metres from the actor within which it can be used (0 = no limit).
    virtual float Range() const = 0;
    virtual bool CanInteract(const InteractContext& c) const = 0;
    /// What the prompt says ("E to pick up Crate"); already localized where the component localizes.
    virtual std::string Prompt(const InteractContext& c) const = 0;
    /// Use it. Returns true when it did something.
    virtual bool Interact(const InteractContext& c) = 0;
    virtual void Inspect(const InteractContext&) {}
    /// Seconds the key must be held (0 = a tap).
    virtual float HoldSeconds() const { return 0.0f; }
    virtual const char* Model() const = 0;
};

namespace interact {

/// ---- The interact key ----
/// The `Interact` input action (default E, pad X; a player can rebind it) is what every interactable reads.
/// A component's own `interactKey` character is kept for old scenes: 0 means "not interactable", 'e' (the
/// old default) means "the Interact action", any other letter was chosen on purpose and stays a raw key.
bool Pressed(char legacyKey);
/// The name to show for that key: "E", or whatever the player rebound the action to ("F", "X").
std::string KeyLabel(char legacyKey);

/// ---- One key press is used once ----
/// The first thing that handles a press calls Block(); everything else in the frame checks Blocked() and
/// leaves it alone. ClearBlock() runs at the end of the frame (ItemPickup and HoldPrompt do it in LateUpdate).
void Block();
bool Blocked();
void ClearBlock();
/// Pressed(key) and not already used this frame; marks it used. Use it where a press should do one thing.
bool Take(char legacyKey);

/// ---- Finding what can be used ----
/// Add the interactables one object has to `out`.
using Finder = std::function<void(GameObject*, std::vector<std::unique_ptr<IInteractable>>& out)>;
void AddFinder(const std::string& name, Finder f);
void ClearFinders();   // tests

std::vector<std::unique_ptr<IInteractable>> Find(GameObject* go);
/// The interactable nearest `actor` that can be used now (within its range), or null. `out` receives the
/// distance when given.
std::unique_ptr<IInteractable> Nearest(Scene& scene, GameObject* actor, float* distance = nullptr);
/// Use whatever is nearest. True when something was used (and the press is marked used).
bool Use(Scene& scene, GameObject* actor);

} // namespace interact
} // namespace okay
