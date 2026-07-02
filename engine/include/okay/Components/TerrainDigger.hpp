#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Game.hpp"
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
    int   button    = 0;        ///< mouse button held for the primary action (mode); <0 = none
    /// Secondary button that RAISES the terrain (so the same digger both digs and
    /// builds up ground): left mouse runs `mode`, right mouse raises by default.
    int   raiseButton = 1;
    char  key       = 0;        ///< optional key that also acts (0 = none)
    float radius    = 3.0f;     ///< brush radius (world units)
    float strength  = 6.0f;     ///< height change per second while held (Dig/Raise)
    float range     = 60.0f;    ///< how far the aim ray reaches before giving up
    float relax     = 6.0f;     ///< Smooth/Flatten rate per second (0..~ -> per-frame amount)
    float hardness  = 0.4f;     ///< brush falloff hardness (0 soft .. 1 hard edge)

    /// Show a ring marker on the ground where the brush will act, so you can see
    /// exactly where (and how big) you're about to dig — like Minecraft's block
    /// highlight, but a terrain brush outline.
    bool  showBrush   = true;
    Color brushColor  = Color::FromBytes(90, 220, 255, 235);  ///< marker tint

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
        // Cameras look down their local -Z (Vec3::Forward is +Z, i.e. *behind* the
        // camera), so the aim ray is -Forward() — matching the screen-centre
        // crosshair and BlockBuilder. (Was un-negated, so the digger aimed behind
        // you and never hit the terrain in front — "digger doesn't work".)
        Vec3 rd = ct->Forward() * -1.0f;
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
        if (Game::Paused()) { HideBrush(); return; }   // no aim marker / sculpting while paused
        Terrain* terr = FindTerrain();
        if (!terr) { HideBrush(); return; }

        // Always aim (even when not acting) so the brush marker tracks the cursor.
        float lx = 0.0f, lz = 0.0f;
        bool aiming = AimAtTerrain(terr, lx, lz);
        if (showBrush && aiming) ShowBrushAt(*terr, lx, lz);
        else HideBrush();

        bool primary = (button >= 0 && Input::GetMouseButton(button)) || (key && Input::GetKey(key));
        bool raise = (raiseButton >= 0 && Input::GetMouseButton(raiseButton));
        if ((!primary && !raise) || dt <= 0.0f || !aiming) return;

        const float amt = strength * dt;
        const float rate = std::min(1.0f, relax * dt);
        // Secondary button always raises; otherwise run the configured mode.
        Mode m = (raise && !primary) ? Mode::Raise : mode;
        switch (m) {
            case Mode::Dig:     terr->RaiseAt(lx, lz, radius, -amt, hardness); break;
            case Mode::Raise:   terr->RaiseAt(lx, lz, radius,  amt, hardness); break;
            case Mode::Smooth:  terr->SmoothAt(lx, lz, radius, rate, hardness); break;
            case Mode::Flatten: terr->FlattenAt(lx, lz, radius, terr->SampleHeight(lx, lz), rate, hardness); break;
        }
        terr->Apply();   // rebuild the live mesh so the dig shows immediately
        if (brush_) ShowBrushAt(*terr, lx, lz);   // re-seat the marker on the new surface
    }

    void OnDestroy() override {
        if (brush_) { if (Scene* s = GetScene()) s->Destroy(brush_); brush_ = nullptr; }
    }

private:
    GameObject* brush_ = nullptr;   ///< runtime-only ring marker (not saved/removable)

    /// A flat ring laid on the ground at the aim point, scaled to the brush radius.
    void ShowBrushAt(Terrain& terr, float lx, float lz) {
        Scene* s = GetScene();
        if (!s || !terr.gameObject || !terr.gameObject->transform) return;
        GameObject* b = EnsureBrush(*s);
        if (!b || !b->transform) return;
        Vec3 tp = terr.gameObject->transform->Position();
        float surf = terr.SampleHeight(lx, lz);
        b->active = true;
        b->transform->localPosition = {tp.x + lx, tp.y + surf + 0.05f, tp.z + lz};
        // Tube mesh has outer radius 0.5, so scale by 2*radius to span the brush.
        b->transform->localScale = {radius * 2.0f, 1.0f, radius * 2.0f};
        if (auto* mr = b->GetComponent<MeshRenderer>()) mr->color = brushColor;
    }
    void HideBrush() { if (brush_) brush_->active = false; }

    GameObject* EnsureBrush(Scene& s) {
        if (brush_) return brush_;
        GameObject* b = s.CreateGameObject("TerrainBrush");
        if (!b) return nullptr;
        b->tag = "TerrainBrush";       // not the terrain -> never dug or saved
        auto* mr = b->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Tube(0.5f, 0.42f, 0.04f, 32);   // thin flat washer = ground ring
        mr->unlit = true;
        mr->shader = MeshRenderer::Shader::Unlit;
        mr->doubleSided = true;
        mr->color = brushColor;
        // No collider: it never blocks the aim ray or counts as terrain.
        brush_ = b;
        return b;
    }
};

} // namespace okay
