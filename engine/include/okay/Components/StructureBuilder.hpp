#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Crosshair.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Math/Quat.hpp"
#include <cmath>
#include <string>

namespace okay {

/// Rust-style structural building: place tiered building pieces that SNAP to a grid
/// and to each other's sockets — foundations on the ground, walls on foundation
/// edges, floors/ceilings a storey up, pillars on corners, ramps off an edge. Look
/// where you want it (a ghost shows the snapped piece, green = valid, red = blocked),
/// left-click to build, right-click to demolish. Number keys 1–5 pick the piece,
/// R rotates. Attach it to the player (or its camera) — it casts from the main
/// camera (centre-screen) and ignores your own body. Placed pieces are real objects
/// (mesh + collider) tagged `structureTag`, so they save with the scene.
class StructureBuilder : public Behaviour {
public:
    enum class Piece { Foundation = 0, Wall = 1, Floor = 2, Pillar = 3, Ramp = 4 };

    Piece       piece          = Piece::Foundation;   ///< current building piece
    float       cellSize       = 3.0f;                ///< foundation/floor footprint (world units)
    float       wallHeight     = 3.0f;                ///< wall / pillar height (one storey)
    float       slabThickness  = 0.3f;                ///< foundation / floor thickness
    float       wallThickness  = 0.2f;                ///< wall thickness
    float       pillarThickness = 0.4f;               ///< pillar cross-section
    float       reach          = 8.0f;                ///< max build/demolish distance
    Color       color          = Color::FromBytes(150, 140, 120);
    std::string structureTexture;                      ///< optional texture for placed pieces
    std::string structureTag   = "Structure";         ///< tag placed pieces carry (only these demolish)
    int         rotSteps       = 0;                    ///< yaw in 90° steps (R to rotate)
    int         placeButton    = 0;                    ///< mouse button to build (0 = left)
    int         removeButton   = 1;                    ///< mouse button to demolish (1 = right)
    char        rotateKey      = 'r';                  ///< rotate the current piece 90°
    bool        pieceHotkeys   = true;                 ///< 1–5 select Foundation/Wall/Floor/Pillar/Ramp
    bool        showPreview    = true;                  ///< ghost of the snapped piece
    bool        showCrosshair  = true;                  ///< auto-add an aim reticle
    Color       previewFree    = Color::FromBytes(80, 255, 120, 230);
    Color       previewBusy    = Color::FromBytes(255, 80, 80, 230);

    /// The snapped pose of a piece for a given aim ray — the testable heart of the
    /// builder. `show` = there's somewhere to draw the ghost; `valid` = a click would
    /// actually build there (somewhere to snap to and not already occupied).
    struct Placement {
        bool  show  = false;
        bool  valid = false;
        Piece piece = Piece::Foundation;
        Vec3  position{0, 0, 0};
        Vec3  scale{1, 1, 1};
        float yaw   = 0.0f;     ///< degrees about Y
        float pitch = 0.0f;     ///< degrees about X (ramps)
    };

    void Update(float) override {
        if (!gameObject) return;
        Scene* s = GetScene();
        if (!s || !s->mainCamera || !s->mainCamera->gameObject || !s->mainCamera->gameObject->transform) return;
        Transform* cam = s->mainCamera->gameObject->transform;
        // Cameras look down their local -Z (Vec3::Forward is +Z, behind the camera).
        const Vec3 origin = cam->Position(), dir = cam->Forward() * -1.0f;
        const float r = reach + CameraGap(origin);

        if (showCrosshair && !crosshairChecked_) { crosshairChecked_ = true; EnsureCrosshair(*s); }

        if (pieceHotkeys) {
            if (Input::GetKeyDown('1')) piece = Piece::Foundation;
            if (Input::GetKeyDown('2')) piece = Piece::Wall;
            if (Input::GetKeyDown('3')) piece = Piece::Floor;
            if (Input::GetKeyDown('4')) piece = Piece::Pillar;
            if (Input::GetKeyDown('5')) piece = Piece::Ramp;
        }
        if (rotateKey && Input::GetKeyDown(rotateKey)) rotSteps = (rotSteps + 1) & 3;

        Placement p = Resolve(*s, origin, dir, r);
        if (showPreview) UpdatePreview(*s, p);
        else if (preview_) preview_->active = false;

        if (Input::GetMouseButtonDown(placeButton) && p.valid) PlacePiece(*s, p);
        else if (Input::GetMouseButtonDown(removeButton))      Demolish(*s, origin, dir, r);
    }

    /// Resolve where the current piece snaps along a ray (camera-independent → testable).
    Placement Resolve(Scene& s, const Vec3& origin, const Vec3& dir, float reachArg = -1.0f) {
        Placement r; r.piece = piece;
        const float maxd = reachArg >= 0.0f ? reachArg : reach;
        RaycastHit3D hit = s.physics3D().Raycast(s, origin, dir, maxd, Owner());
        if (!hit.hit || !hit.gameObject) return r;
        const float yaw = (float)(rotSteps & 3) * 90.0f;

        // Foundations snap to a world grid and sit on whatever surface you aim at.
        if (piece == Piece::Foundation) {
            float c = cellSize > 1e-3f ? cellSize : 1.0f;
            Vec3 pos{std::round(hit.point.x / c) * c,
                     hit.point.y + slabThickness * 0.5f,
                     std::round(hit.point.z / c) * c};
            r.position = pos; r.scale = {c, slabThickness, c}; r.yaw = yaw; r.show = true;
            r.valid = !Occupied(s, pos, c * 0.3f);
            return r;
        }

        // Everything else snaps to an existing structure piece.
        GameObject* tgt = hit.gameObject;
        if (!tgt || tgt->tag != structureTag || !tgt->transform) return r;
        const Vec3  center = tgt->transform->Position();
        const Vec3  ts     = tgt->transform->localScale;
        const float top    = center.y + ts.y * 0.5f;
        const bool  slab   = ts.y <= ts.x * 0.6f && ts.y <= ts.z * 0.6f;   // foundation/floor
        const Vec3  d      = hit.point - center;

        if (piece == Piece::Wall) {
            if (!slab) return r;
            if (std::fabs(d.x) >= std::fabs(d.z)) {            // east/west edge: wall runs along Z
                float sx = d.x >= 0 ? 1.0f : -1.0f;
                r.position = {center.x + sx * ts.x * 0.5f, top + wallHeight * 0.5f, center.z};
                r.scale = {wallThickness, wallHeight, ts.z};
            } else {                                           // north/south edge: wall runs along X
                float sz = d.z >= 0 ? 1.0f : -1.0f;
                r.position = {center.x, top + wallHeight * 0.5f, center.z + sz * ts.z * 0.5f};
                r.scale = {ts.x, wallHeight, wallThickness};
            }
            r.show = true; r.valid = !Occupied(s, r.position, 0.35f);
            return r;
        }
        if (piece == Piece::Pillar) {
            if (!slab) return r;
            float sx = d.x >= 0 ? 1.0f : -1.0f, sz = d.z >= 0 ? 1.0f : -1.0f;
            r.position = {center.x + sx * ts.x * 0.5f, top + wallHeight * 0.5f, center.z + sz * ts.z * 0.5f};
            r.scale = {pillarThickness, wallHeight, pillarThickness};
            r.show = true; r.valid = !Occupied(s, r.position, pillarThickness * 0.6f);
            return r;
        }
        if (piece == Piece::Floor) {
            if (!slab) return r;                               // stack a storey above this tile
            r.position = {center.x, top + wallHeight + slabThickness * 0.5f, center.z};
            r.scale = {ts.x, slabThickness, ts.z}; r.yaw = yaw;
            r.show = true; r.valid = !Occupied(s, r.position, ts.x * 0.3f);
            return r;
        }
        if (piece == Piece::Ramp) {
            if (!slab) return r;                               // an inclined slab off the nearest edge
            float c = cellSize > 1e-3f ? cellSize : 1.0f;
            if (std::fabs(d.x) >= std::fabs(d.z)) {
                float sx = d.x >= 0 ? 1.0f : -1.0f;
                r.position = {center.x + sx * c, top + wallHeight * 0.5f, center.z};
                r.yaw = sx > 0 ? 0.0f : 180.0f;
            } else {
                float sz = d.z >= 0 ? 1.0f : -1.0f;
                r.position = {center.x, top + wallHeight * 0.5f, center.z + sz * c};
                r.yaw = sz > 0 ? 90.0f : 270.0f;
            }
            r.scale = {c, slabThickness, c * 1.5f}; r.pitch = 35.0f;
            r.show = true; r.valid = !Occupied(s, r.position, c * 0.3f);
            return r;
        }
        return r;
    }

    GameObject* PlacePiece(Scene& s, const Placement& p) {
        if (!p.valid) return nullptr;
        const char* names[] = {"Foundation", "Wall", "Floor", "Pillar", "Ramp"};
        GameObject* g = s.CreateGameObject(names[(int)p.piece]);
        if (!g) return nullptr;
        g->tag = structureTag;
        g->transform->localPosition = p.position;
        g->transform->localRotation = Quat::Euler(p.pitch, p.yaw, 0.0f);
        g->transform->localScale = p.scale;
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->color = color;
        if (!structureTexture.empty()) mr->texture = structureTexture;
        g->AddComponent<BoxCollider3D>();
        return g;
    }

    GameObject* Demolish(Scene& s, const Vec3& origin, const Vec3& dir, float reachArg = -1.0f) {
        const float maxd = reachArg >= 0.0f ? reachArg : reach;
        RaycastHit3D hit = s.physics3D().Raycast(s, origin, dir, maxd, Owner());
        if (hit.hit && hit.gameObject && hit.gameObject->tag == structureTag) {
            GameObject* g = hit.gameObject; s.Destroy(g); return g;
        }
        return nullptr;
    }

private:
    GameObject* preview_ = nullptr;
    bool        crosshairChecked_ = false;

    GameObject* Owner() const {
        if (!gameObject || !gameObject->transform) return gameObject;
        Transform* t = gameObject->transform;
        while (t->Parent()) t = t->Parent();
        return t->gameObject ? t->gameObject : gameObject;
    }
    float CameraGap(const Vec3& camPos) const {
        GameObject* o = Owner();
        if (!o || !o->transform) return 0.0f;
        Vec3 d = camPos - o->transform->Position();
        return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    }

    void UpdatePreview(Scene& s, const Placement& p) {
        GameObject* g = EnsurePreview(s);
        if (!g || !g->transform) return;
        if (!p.show) { g->active = false; return; }
        g->active = true;
        g->transform->localPosition = p.position;
        g->transform->localRotation = Quat::Euler(p.pitch, p.yaw, 0.0f);
        // A hair larger so the outline hugs the piece without z-fighting.
        g->transform->localScale = {p.scale.x * 1.02f + 0.02f, p.scale.y * 1.02f + 0.02f, p.scale.z * 1.02f + 0.02f};
        if (auto* mr = g->GetComponent<MeshRenderer>())
            mr->color = p.valid ? previewFree : previewBusy;
    }
    GameObject* EnsurePreview(Scene& s) {
        if (preview_) return preview_;
        GameObject* g = s.CreateGameObject("StructurePreview");
        if (!g) return nullptr;
        g->tag = "StructurePreview";        // not structureTag → not demolished/counted
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->wireframe = true; mr->unlit = true;
        mr->shader = MeshRenderer::Shader::Unlit;
        mr->color = previewFree;
        preview_ = g;
        return g;
    }
    void EnsureCrosshair(Scene& s) {
        for (const auto& up : s.Objects())
            if (up && up->GetComponent<Crosshair>()) return;
        if (gameObject && !gameObject->GetComponent<Crosshair>())
            gameObject->AddComponent<Crosshair>()->dot = true;
    }
    bool Occupied(Scene& s, const Vec3& pos, float eps) const {
        for (const auto& up : s.Objects()) {
            GameObject* g = up.get();
            if (!g || g->tag != structureTag || !g->transform) continue;
            Vec3 q = g->transform->Position();
            if (std::fabs(q.x - pos.x) < eps && std::fabs(q.y - pos.y) < eps && std::fabs(q.z - pos.z) < eps)
                return true;
        }
        return false;
    }
};

} // namespace okay
