#pragma once
// Does the scene have anything drawn on top of the 3D picture that has to hide behind solid meshes (particles, lines, trails)?
// Only then does a host that renders on the GPU pay for the software depth pass (FillOcclusionDepth). A ParticleSystem that
// exists but has no live particle (the Voxel Digger's dust object between clicks, a muzzle flash at rest) does not count: the
// pass used to run every frame from the first dig onward and cost about as much as a whole software frame.
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/LineRenderer.hpp"

namespace okay {

inline bool SceneHasOverlayNeedingDepth(const Scene& scene) {
    for (const auto& up : scene.Objects()) {
        if (!up || !up->active) continue;
        if (up->GetComponent<LineRenderer>() || up->GetComponent<TrailRenderer>()) return true;
        if (auto* ps = up->GetComponent<ParticleSystem>())
            for (const auto& p : ps->Particles()) if (p.alive) return true;
    }
    return false;
}

} // namespace okay
