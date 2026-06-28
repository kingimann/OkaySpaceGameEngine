#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Input/Input.hpp"
#include <cmath>
#include <algorithm>

namespace okay {

/// Runtime terrain digging / sculpting for a heightmap Terrain — "dig into the
/// ground" the smooth way (craters, trenches, mounds), NOT Minecraft cubes. Aim
/// with the main camera's crosshair and hold the button to lower (Dig), raise,
/// smooth, or flatten the terrain under the cursor; the mesh rebuilds live each
/// frame the brush is active.
///
/// Heightmap = a single top surface, so you can carve holes / valleys and build up
/// hills, but NOT caves, tunnels or overhangs (those need a 3D voxel density field).
/// Drop this on the Terrain object (it digs itself) or anywhere in the scene (it
/// finds the first Terrain). The main Camera supplies the aim ray.
class TerrainDigger : public Behaviour {
public:
    enum class Mode { Dig, Raise, Smooth, Flatten };
    Mode  mode      = Mode::Dig;
    int   button    = 0;        ///< mouse button held to act (0=left, 1=right, 2=middle); <0 = none
    char  key       = 0;        ///< optional key that also acts (0 = none)
    float radius    = 3.0f;     ///< brush radius (world units)
    float strength  = 6.0f;     ///< height change per second while held (Dig/Raise)
    float range     = 60.0f;    ///< how far the aim ray reaches before giving up
    float relax     = 6.0f;     ///< Smooth/Flatten rate per second (0..~ -> per-frame amount)

    /// The Terrain this digs: on the same object if present, else the first in the scene.
    Terrain* FindTerrain() const {
        if (gameObject)
            if (auto* t = gameObject->GetComponent<Terrain>()) return t;
        Scene* s = GetScene();
        return s ? s->FindObjectOfType<Terrain>() : nullptr;
    }

    /// March the aim ray and return the terrain-local XZ where it first dips below the
    /// surface. Returns false if it never hits within `range` / the terrain bounds.
    bool AimAtTerrain(Terrain* terr, float& outLX, float& outLZ) const {
        if (!terr || !terr->gameObject) return false;
        Scene* s = GetScene();
        Camera* cam = s ? s->FindObjectOfType<Camera>() : nullptr;   // main camera
        if (!cam || !cam->gameObject || !cam->gameObject->transform) return false;
        Transform* ct = cam->gameObject->transform;
        Vec3 ro = ct->Position();
        Vec3 rd = ct->Forward();
        { float m = std::sqrt(rd.x * rd.x + rd.y * rd.y + rd.z * rd.z); if (m > 1e-6f) rd = rd * (1.0f / m); }
        Transform* tt = terr->gameObject->transform;
        Vec3 tp = tt->Position();
        const float half = terr->size * 0.5f;
        float step = radius * 0.25f; if (step < 0.25f) step = 0.25f;
        for (float d = 0.0f; d <= range; d += step) {
            Vec3 p = ro + rd * d;
            float lx = p.x - tp.x, lz = p.z - tp.z;   // terrain assumed axis-aligned (no rotation)
            if (lx < -half || lx > half || lz < -half || lz > half) continue;
            float surfY = tp.y + terr->SampleHeight(lx, lz);
            if (p.y <= surfY) { outLX = lx; outLZ = lz; return true; }
        }
        return false;
    }

    void Update(float dt) override {
        bool act = false;
        if (button >= 0 && Input::GetMouseButton(button)) act = true;
        if (key && Input::GetKey(key)) act = true;
        if (!act || dt <= 0.0f) return;
        Terrain* terr = FindTerrain();
        if (!terr) return;
        float lx = 0.0f, lz = 0.0f;
        if (!AimAtTerrain(terr, lx, lz)) return;
        const float amt = strength * dt;
        const float rate = std::min(1.0f, relax * dt);
        switch (mode) {
            case Mode::Dig:     terr->RaiseAt(lx, lz, radius, -amt); break;
            case Mode::Raise:   terr->RaiseAt(lx, lz, radius,  amt); break;
            case Mode::Smooth:  terr->SmoothAt(lx, lz, radius, rate); break;
            case Mode::Flatten: terr->FlattenAt(lx, lz, radius, terr->SampleHeight(lx, lz), rate); break;
        }
        terr->Apply();   // rebuild the live mesh so the dig shows immediately
    }
};

} // namespace okay
