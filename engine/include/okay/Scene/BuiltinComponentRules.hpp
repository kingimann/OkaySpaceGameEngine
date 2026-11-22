#pragma once
// The engine's own RequireComponent / DisallowMultipleComponent rules (Scene/GameObject.hpp has the mechanism).
// The editor and the player call RegisterBuiltinComponentRules() once at startup; from then on AddComponent (scripts,
// the editor's Add Component menu, the scene loader) adds what a component needs first, and an editor can ask
// GameObject::RequiredBy before removing one.

namespace okay {

/// Register the built-in rules. Calling it again does nothing more.
void RegisterBuiltinComponentRules();

} // namespace okay
