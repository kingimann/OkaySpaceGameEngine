#pragma once
// Which ParticleSystems a host draws this frame. The editor's Scene view (2D and 3D) and the player's 2D and 3D passes all
// ask this one function, so a headless test can check what they would submit.
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include <memory>
#include <vector>

namespace okay {

/// Append to `out` the particle systems to draw from `objs`, in scene order: EVERY ParticleSystem on an object, in its
/// component order (sparks and smoke on one fire; the hosts used to draw only the first). Inactive objects are left out, and so is any
/// object `skip(go)` (a GameObject*) returns true for (the hosts pass their "hidden UI object" check).
template <class Skip>
void CollectParticleSystemsToDraw(const std::vector<std::unique_ptr<GameObject>>& objs, std::vector<ParticleSystem*>& out, Skip&& skip) {
    for (const auto& up : objs) {
        if (!up || !up->active || skip(up.get())) continue;
        up->CollectComponents(out);
    }
}
inline void CollectParticleSystemsToDraw(const std::vector<std::unique_ptr<GameObject>>& objs, std::vector<ParticleSystem*>& out) {
    CollectParticleSystemsToDraw(objs, out, [](const GameObject*) { return false; });
}

} // namespace okay
