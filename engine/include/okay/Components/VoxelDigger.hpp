#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Input/Input.hpp"
#include <cmath>

namespace okay {

/// Runtime digging for a VoxelTerrain — carve real caves, tunnels and overhangs
/// (the smooth, non-Minecraft kind). Aim with the main camera and hold the button
/// to subtract material (Dig) or add it (Add); the marching-cubes surface re-skins
/// live each frame. A sphere marker shows where you're aiming.
///
/// Drop it on the VoxelTerrain object (it digs itself) or anywhere in the scene
/// (it finds the first VoxelTerrain). The main Camera supplies the aim ray.
class VoxelDigger : public Behaviour {
public:
    enum class Mode { Dig, Add };
    Mode  mode     = Mode::Dig;
    int   button   = 0;        ///< mouse button held for the primary action (mode); <0 = none
    /// Secondary button for the OPPOSITE action, so one digger both digs AND
    /// raises out of the box: left mouse digs, right mouse adds material back.
    int   addButton = 1;
    char  key      = 0;        ///< optional key that also acts (0 = none)
    float radius   = 2.5f;     ///< brush radius (world units)
    float strength = 8.0f;     ///< density change per second while held
    float range    = 80.0f;    ///< how far the aim ray reaches
    bool  showBrush  = true;
    Color brushColor = Color::FromBytes(255, 180, 70, 200);

    VoxelTerrain* FindVoxel() const {
        if (gameObject)
            if (auto* v = gameObject->GetComponent<VoxelTerrain>()) return v;
        Scene* s = GetScene();
        return s ? s->FindObjectOfType<VoxelTerrain>() : nullptr;
    }

    /// March the camera aim ray and return the local-space point where it first
    /// enters solid voxel (Dig) or the last air point before it (Add). Returns
    /// false if the ray never finds the surface within `range`.
    bool AimAtVoxel(VoxelTerrain* vox, Vec3& outLocal) const {
        if (!vox || !vox->gameObject || !vox->gameObject->transform) return false;
        Scene* s = GetScene();
        Camera* cam = s ? s->FindObjectOfType<Camera>() : nullptr;
        if (!cam || !cam->gameObject || !cam->gameObject->transform) return false;
        Transform* ct = cam->gameObject->transform;
        Vec3 ro = ct->Position();
        Vec3 rd = ct->Forward() * -1.0f;   // aim is -Forward (see BlockBuilder)
        { float m = std::sqrt(rd.x * rd.x + rd.y * rd.y + rd.z * rd.z); if (m > 1e-6f) rd = rd * (1.0f / m); }
        Vec3 vp = vox->gameObject->transform->Position();
        float step = vox->voxelSize * 0.4f; if (step < 0.05f) step = 0.05f;
        Vec3 prevLocal{0, 0, 0}; bool havePrev = false;
        for (float d = 0.0f; d <= range; d += step) {
            Vec3 p = ro + rd * d;
            Vec3 local{p.x - vp.x, p.y - vp.y, p.z - vp.z};
            if (vox->SampleDensity(local) > vox->iso) {     // hit solid
                outLocal = (mode == Mode::Add && havePrev) ? prevLocal : local;
                return true;
            }
            prevLocal = local; havePrev = true;
        }
        return false;
    }

    void Update(float dt) override {
        VoxelTerrain* vox = FindVoxel();
        if (!vox) { HideBrush(); return; }
        Vec3 local;
        bool aiming = AimAtVoxel(vox, local);
        if (showBrush && aiming) ShowBrushAt(*vox, local);
        else HideBrush();

        // Primary button does `mode`; the secondary button does the opposite — so
        // the player can both dig and raise (add) terrain with one component.
        bool primary = (button >= 0 && Input::GetMouseButton(button)) || (key && Input::GetKey(key));
        bool secondary = (addButton >= 0 && Input::GetMouseButton(addButton));
        if ((!primary && !secondary) || dt <= 0.0f || !aiming) return;

        Mode m = mode;
        if (secondary && !primary) m = (mode == Mode::Dig) ? Mode::Add : Mode::Dig;

        float amt = strength * dt;
        if (m == Mode::Dig) vox->Dig(local, radius, amt);
        else                vox->Add(local, radius, amt);
        vox->Apply();   // re-skin the surface immediately
    }

    void OnDestroy() override {
        if (brush_) { if (Scene* s = GetScene()) s->Destroy(brush_); brush_ = nullptr; }
    }

private:
    GameObject* brush_ = nullptr;

    void ShowBrushAt(VoxelTerrain& vox, const Vec3& local) {
        Scene* s = GetScene();
        if (!s || !vox.gameObject || !vox.gameObject->transform) return;
        GameObject* b = EnsureBrush(*s);
        if (!b || !b->transform) return;
        Vec3 vp = vox.gameObject->transform->Position();
        b->active = true;
        b->transform->localPosition = {vp.x + local.x, vp.y + local.y, vp.z + local.z};
        float dscale = radius * 2.0f;   // sphere mesh has radius 0.5
        b->transform->localScale = {dscale, dscale, dscale};
        if (auto* mr = b->GetComponent<MeshRenderer>()) mr->color = brushColor;
    }
    void HideBrush() { if (brush_) brush_->active = false; }

    GameObject* EnsureBrush(Scene& s) {
        if (brush_) return brush_;
        GameObject* b = s.CreateGameObject("VoxelBrush");
        if (!b) return nullptr;
        b->tag = "VoxelBrush";
        auto* mr = b->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Sphere(0.5f, 8, 12);
        mr->wireframe = true;          // hollow outline so you see through it
        mr->unlit = true;
        mr->shader = MeshRenderer::Shader::Unlit;
        mr->color = brushColor;
        brush_ = b;
        return b;
    }
};

} // namespace okay
