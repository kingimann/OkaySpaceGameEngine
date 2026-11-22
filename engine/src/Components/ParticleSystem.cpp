// Particle System: the parts that need other components (terrain collision). Written for this engine from the
// height-field sampling Terrain already offers; the response is the usual split of the velocity into the part
// along the surface normal and the part along the ground.
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

#include <cmath>

namespace okay {

void ParticleSystem::GatherGround() {
    m_ground.clear();
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return;
    for (Terrain* t : sc->FindObjectsOfType<Terrain>()) {
        if (!t || !t->gameObject || !t->gameObject->ActiveInHierarchy() || !t->transform) continue;
        if (t->heights.empty() || !(t->size > 0.0f)) continue;
        ParticleGround g;
        g.terrain = t;
        g.toWorld = t->transform->LocalToWorldMatrix();
        g.toLocal = g.toWorld.Inverse();
        g.half = t->size * 0.5f;
        m_ground.push_back(g);
    }
}

bool ParticleSystem::GroundHit(const Vec3& pos, Vec3& surface, Vec3& normal) const {
    bool hit = false;
    float bestY = 0.0f;
    for (const ParticleGround& g : m_ground) {
        const Vec3 l = g.toLocal.MultiplyPoint(pos);
        if (!(std::fabs(l.x) <= g.half) || !(std::fabs(l.z) <= g.half)) continue;   // off the terrain (or NaN)
        const float h = g.terrain->SampleHeight(l.x, l.z);
        if (!(l.y < h)) continue;                                                     // above the ground
        const Vec3 w = g.toWorld.MultiplyPoint({l.x, h, l.z});
        if (hit && w.y <= bestY) continue;                                            // a higher terrain wins
        // A normal goes to world space by the transpose of the world-to-local matrix (right under any scale).
        const Vec3 ln = g.terrain->NormalAt(l.x, l.z);
        const Mat4& M = g.toLocal;
        const Vec3 n{M.at(0, 0) * ln.x + M.at(0, 1) * ln.y + M.at(0, 2) * ln.z,
                     M.at(1, 0) * ln.x + M.at(1, 1) * ln.y + M.at(1, 2) * ln.z,
                     M.at(2, 0) * ln.x + M.at(2, 1) * ln.y + M.at(2, 2) * ln.z};
        const float m = n.Magnitude();
        normal = m > 1e-6f ? n * (1.0f / m) : Vec3{0.0f, 1.0f, 0.0f};
        surface = w;
        bestY = w.y;
        hit = true;
    }
    return hit;
}

} // namespace okay
