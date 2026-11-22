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

/// The key itself is main's shared Interact action (okay/Input/GameInput.hpp: InteractPressed, UseKeyDown,
/// UseKeyName), and "one press is used once" is ItemPickup::BlockInteract. This file only finds things to use.

/// ---- Finding what can be used ----
/// Add the interactables one object has to `out`.
using Finder = std::function<void(GameObject*, std::vector<std::unique_ptr<IInteractable>>& out)>;
void AddFinder(const std::string& name, Finder f);
void ClearFinders();   // tests

std::vector<std::unique_ptr<IInteractable>> Find(GameObject* go);
/// The interactable nearest `actor` that can be used now (within its range), or null. `out` receives the
/// distance when given.
std::unique_ptr<IInteractable> Nearest(Scene& scene, GameObject* actor, float* distance = nullptr);

} // namespace interact
} // namespace okay
