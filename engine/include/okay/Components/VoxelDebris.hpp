#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include <cmath>
#include <functional>

namespace okay {

/// A loose piece of dug ground: a small rigid body that carries a volume of one material. It falls, bounces and lies
/// where it lands. When it settles (or its life runs out, or the player walks up to it) the volume is not lost: it is
/// put back into the terrain where it lies, or into the digger's store (`toStore`). Spawned by the Voxel Digger.
/// The landing spot is worked out in the terrain's own space, so a terrain that is moved, turned or scaled works.
class VoxelDebris : public Behaviour {
public:
    std::uint64_t terrainId = 0;      ///< GameObject id of the VoxelTerrain
    int    material = 0;
    double volume = 0.0;              ///< cubic metres
    bool   toStore = false;           ///< finish into `onStore` instead of the terrain
    float  life = 20.0f;              ///< seconds before it is put away whatever it is doing
    float  pickupRange = 2.2f;        ///< a ToStore piece this close to the main camera is collected
    std::function<void(int material, double volume)> onStore;
    float  age = 0.0f, still = 0.0f;
    bool   finished = false;
    static int& Live() { static int n = 0; return n; }

    // Counted from the moment it is made (not at Start), so a cap on live pieces holds even inside one frame.
    VoxelDebris() { ++Live(); counted_ = true; }
    ~VoxelDebris() override { Uncount(); }
    void OnDestroy() override { Uncount(); }
    void ReleaseUnstarted() override { Uncount(); }

    void Update(float dt) override {
        if (finished || !gameObject || !gameObject->transform) return;
        age += dt;
        auto* rb = gameObject->GetComponent<Rigidbody3D>();
        const float speed = rb ? rb->velocity.Magnitude() : 0.0f;
        still = (rb && (rb->sleeping || speed < 0.15f)) ? still + dt : 0.0f;
        bool closeToCamera = false;
        if (toStore) {
            Scene* s = GetScene();
            if (s && s->mainCamera && s->mainCamera->gameObject && s->mainCamera->gameObject->transform)
                closeToCamera = (s->mainCamera->gameObject->transform->Position() - gameObject->transform->Position()).Magnitude() < pickupRange;
        }
        if (still > 1.2f || age > life || closeToCamera) Finish();
    }

    /// Put the volume away now (also what the tests call).
    void Finish() {
        if (finished) return;
        finished = true;
        Scene* s = GetScene();
        if (toStore && onStore) onStore(material, volume);
        else if (GameObject* tg = GameObject::FromInstanceId(terrainId)) {
            if (auto* t = tg->GetComponent<VoxelTerrain>(); t && tg->transform) {
                const Vec3 p = tg->transform->InverseTransformPoint(gameObject->transform->Position());   // terrain may be moved, turned or scaled
                // settle it a little above its centre so it merges with the ground it rests on
                const double got = t->AddVolume({p.x, p.y, p.z}, volume, material, 0.8f);
                (void)got;
                t->Apply();
            }
        }
        if (s && gameObject) s->Destroy(gameObject);
    }

private:
    void Uncount() { if (counted_) { --Live(); counted_ = false; } }
    bool counted_ = false;
};

} // namespace okay
