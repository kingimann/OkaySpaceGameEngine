#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Crosshair.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/BuildPiece.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Math/Quat.hpp"
#include <cmath>
#include <string>
#include <algorithm>

namespace okay {

/// Rust-style structural building: place tiered pieces that SNAP to a grid and to
/// each other — foundations (square + triangle) on the ground, walls/doorways/
/// windows on foundation edges, floors a storey up, pillars on corners, ramps off
/// an edge. Look where you want it (a ghost shows the snapped piece: green = can
/// build, red = blocked or can't afford), left-click builds, right-click demolishes.
/// Keys 1–8 pick the piece, R rotates, G upgrades the piece you're aiming at
/// (wood → stone → metal), F repairs it. Building/upgrading/repairing draws
/// resources from the player's Inventory (set requireResources = false for a
/// creative mode); demolishing refunds part of the cost. Each placed piece carries
/// a BuildPiece (tier + health), so it saves with the scene and can be raided.
/// Attach to the player (or its camera): it casts from the main camera (centre
/// screen, down the real look axis) and ignores your own body.
class StructureBuilder : public Behaviour {
public:
    enum class Piece { Foundation = 0, FoundationTri = 1, Wall = 2, Doorway = 3,
                       Window = 4, Floor = 5, Pillar = 6, Ramp = 7 };

    Piece       piece          = Piece::Foundation;
    float       cellSize       = 3.0f;
    float       wallHeight     = 3.0f;
    float       slabThickness  = 0.3f;
    float       wallThickness  = 0.2f;
    float       pillarThickness = 0.4f;
    float       reach          = 8.0f;
    std::string structureTexture;
    std::string structureTag   = "Structure";
    int         rotSteps       = 0;
    int         placeButton    = 0;
    int         removeButton    = 1;
    char        rotateKey      = 'r';
    char        upgradeKey     = 'g';
    char        repairKey      = 'f';
    bool        pieceHotkeys   = true;
    bool        showPreview    = true;
    bool        showCrosshair  = true;
    Color       previewFree    = Color::FromBytes(80, 255, 120, 230);
    Color       previewBusy    = Color::FromBytes(255, 80, 80, 230);

    // ---- Resources / tiers (Inventory integration) ----
    bool        requireResources = true;     ///< off = creative (free building)
    float       refundFraction   = 0.5f;     ///< share of cost returned on demolish
    std::string woodItem  = "Wood",  stoneItem = "Stone", metalItem = "Metal";
    int         costWood  = 10, costStone = 15, costMetal = 20;   ///< to build/upgrade INTO each tier

    const std::string& TierMaterial(int t) const { return t <= 0 ? woodItem : (t == 1 ? stoneItem : metalItem); }
    int   TierCost(int t)   const { return t <= 0 ? costWood : (t == 1 ? costStone : costMetal); }
    float TierHealth(int t) const { return t <= 0 ? 200.0f : (t == 1 ? 500.0f : 1000.0f); }
    Color TierColor(int t)  const {
        return t <= 0 ? Color::FromBytes(150, 110, 70)
             : t == 1 ? Color::FromBytes(140, 140, 150)
                      : Color::FromBytes(120, 125, 140);
    }

    struct Placement {
        bool  show  = false;
        bool  valid = false;
        Piece piece = Piece::Foundation;
        Vec3  position{0, 0, 0};
        Vec3  scale{1, 1, 1};   ///< piece dimensions (length, height, depth) in its local frame
        float yaw   = 0.0f;
    };

    void Update(float) override {
        if (!gameObject) return;
        Scene* s = GetScene();
        if (!s || !s->mainCamera || !s->mainCamera->gameObject || !s->mainCamera->gameObject->transform) return;
        Transform* cam = s->mainCamera->gameObject->transform;
        const Vec3 origin = cam->Position(), dir = cam->Forward() * -1.0f;   // cameras look down -Z
        const float r = reach + CameraGap(origin);

        if (showCrosshair && !crosshairChecked_) { crosshairChecked_ = true; EnsureCrosshair(*s); }

        if (pieceHotkeys) {
            if (Input::GetKeyDown('1')) piece = Piece::Foundation;
            if (Input::GetKeyDown('2')) piece = Piece::FoundationTri;
            if (Input::GetKeyDown('3')) piece = Piece::Wall;
            if (Input::GetKeyDown('4')) piece = Piece::Doorway;
            if (Input::GetKeyDown('5')) piece = Piece::Window;
            if (Input::GetKeyDown('6')) piece = Piece::Floor;
            if (Input::GetKeyDown('7')) piece = Piece::Pillar;
            if (Input::GetKeyDown('8')) piece = Piece::Ramp;
        }
        if (rotateKey  && Input::GetKeyDown(rotateKey))  rotSteps = (rotSteps + 1) & 3;
        if (upgradeKey && Input::GetKeyDown(upgradeKey)) Upgrade(*s, origin, dir, r);
        if (repairKey  && Input::GetKeyDown(repairKey))  Repair(*s, origin, dir, r);

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
        const bool affordable = CanAfford(0);   // new pieces are built at wood (tier 0)

        // Foundations (square or triangle) snap to a world grid and sit on the surface.
        if (piece == Piece::Foundation || piece == Piece::FoundationTri) {
            float c = cellSize > 1e-3f ? cellSize : 1.0f;
            Vec3 pos{std::round(hit.point.x / c) * c, hit.point.y + slabThickness * 0.5f,
                     std::round(hit.point.z / c) * c};
            r.position = pos; r.scale = {c, slabThickness, c}; r.yaw = yaw; r.show = true;
            r.valid = affordable && !Occupied(s, pos, c * 0.3f);
            return r;
        }

        GameObject* tgt = hit.gameObject;
        if (!tgt || tgt->tag != structureTag || !tgt->transform) return r;
        const Vec3  center = tgt->transform->Position();
        const Vec3  ts     = tgt->transform->localScale;
        const float top    = center.y + ts.y * 0.5f;
        const bool  slab   = ts.y <= ts.x * 0.6f && ts.y <= ts.z * 0.6f;   // foundation/floor
        const Vec3  d      = hit.point - center;

        // Wall family (wall / doorway / window): snap to the nearest edge, standing on
        // the tile top. Kept AXIS-ALIGNED (box colliders ignore rotation) — the scale
        // shape encodes orientation: thin on X = runs along Z, thin on Z = runs along X.
        if (piece == Piece::Wall || piece == Piece::Doorway || piece == Piece::Window) {
            if (!slab) return r;
            if (std::fabs(d.x) >= std::fabs(d.z)) {            // east/west edge → runs along Z
                float sx = d.x >= 0 ? 1.0f : -1.0f;
                r.position = {center.x + sx * ts.x * 0.5f, top + wallHeight * 0.5f, center.z};
                r.scale = {wallThickness, wallHeight, ts.z};
            } else {                                           // north/south edge → runs along X
                float sz = d.z >= 0 ? 1.0f : -1.0f;
                r.position = {center.x, top + wallHeight * 0.5f, center.z + sz * ts.z * 0.5f};
                r.scale = {ts.x, wallHeight, wallThickness};
            }
            r.show = true; r.valid = affordable && !Occupied(s, r.position, 0.35f);
            return r;
        }
        if (piece == Piece::Pillar) {
            if (!slab) return r;
            float sx = d.x >= 0 ? 1.0f : -1.0f, sz = d.z >= 0 ? 1.0f : -1.0f;
            r.position = {center.x + sx * ts.x * 0.5f, top + wallHeight * 0.5f, center.z + sz * ts.z * 0.5f};
            r.scale = {pillarThickness, wallHeight, pillarThickness};
            r.show = true; r.valid = affordable && !Occupied(s, r.position, pillarThickness * 0.6f);
            return r;
        }
        if (piece == Piece::Floor) {
            if (!slab) return r;
            r.position = {center.x, top + wallHeight + slabThickness * 0.5f, center.z};
            r.scale = {ts.x, slabThickness, ts.z}; r.yaw = yaw;
            r.show = true; r.valid = affordable && !Occupied(s, r.position, ts.x * 0.3f);
            return r;
        }
        if (piece == Piece::Ramp) {
            if (!slab) return r;
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
            r.scale = {c, wallHeight, c}; r.show = true;
            r.valid = affordable && !Occupied(s, r.position, c * 0.3f);
            return r;
        }
        return r;
    }

    GameObject* PlacePiece(Scene& s, const Placement& p) {
        if (!p.valid) return nullptr;
        if (requireResources) { Inventory* inv = Inv(); if (!inv || !inv->Remove(TierMaterial(0), TierCost(0))) return nullptr; }

        const char* names[] = {"Foundation", "FoundationTri", "Wall", "Doorway", "Window", "Floor", "Pillar", "Ramp"};
        GameObject* g = s.CreateGameObject(names[(int)p.piece]);
        if (!g) return nullptr;
        g->tag = structureTag;
        g->transform->localPosition = p.position;
        g->transform->localRotation = Quat::Euler(0.0f, p.yaw, 0.0f);
        const Color col = TierColor(0);

        if (p.piece == Piece::Doorway || p.piece == Piece::Window) {
            // Built from child cube parts (a frame around an opening) so it reuses the
            // proven cube render/collide path; the gap is left open & walkable.
            g->transform->localScale = {1, 1, 1};
            BuildFrame(s, g, p.scale, p.piece == Piece::Window, col);
        } else {
            auto* mr = g->AddComponent<MeshRenderer>();
            mr->color = col;
            if (!structureTexture.empty()) mr->texture = structureTexture;
            mr->mesh = (p.piece == Piece::FoundationTri || p.piece == Piece::Ramp)
                       ? Mesh::Wedge() : Mesh::Cube();   // wedge = flat triangle / ramp
            g->transform->localScale = p.scale;
            g->AddComponent<BoxCollider3D>();
        }

        auto* bp = g->AddComponent<BuildPiece>();
        bp->tier = 0; bp->material = TierMaterial(0); bp->cost = TierCost(0);
        bp->maxHealth = bp->health = TierHealth(0);
        return g;
    }

    /// Demolish the structure piece you're aiming at, refunding part of its cost.
    GameObject* Demolish(Scene& s, const Vec3& origin, const Vec3& dir, float reachArg = -1.0f) {
        const float maxd = reachArg >= 0.0f ? reachArg : reach;
        RaycastHit3D hit = s.physics3D().Raycast(s, origin, dir, maxd, Owner());
        GameObject* g = ResolveStructure(hit.gameObject);
        if (!g) return nullptr;
        if (requireResources) {
            if (Inventory* inv = Inv()) {
                auto* bp = g->GetComponent<BuildPiece>();
                std::string mat = bp ? bp->material : TierMaterial(0);
                int back = bp ? (int)(bp->cost * refundFraction) : (int)(TierCost(0) * refundFraction);
                if (back > 0) inv->Add(mat, back);
            }
        }
        s.Destroy(g);
        return g;
    }

    /// Upgrade the aimed piece to the next tier (wood→stone→metal), paying its cost.
    GameObject* Upgrade(Scene& s, const Vec3& origin, const Vec3& dir, float reachArg = -1.0f) {
        const float maxd = reachArg >= 0.0f ? reachArg : reach;
        RaycastHit3D hit = s.physics3D().Raycast(s, origin, dir, maxd, Owner());
        GameObject* g = ResolveStructure(hit.gameObject);
        if (!g) return nullptr;
        auto* bp = g->GetComponent<BuildPiece>();
        if (!bp || bp->tier >= 2) return nullptr;
        int nt = bp->tier + 1;
        if (requireResources) { Inventory* inv = Inv(); if (!inv || !inv->Remove(TierMaterial(nt), TierCost(nt))) return nullptr; }
        bp->tier = nt; bp->material = TierMaterial(nt); bp->cost = TierCost(nt);
        bp->maxHealth = bp->health = TierHealth(nt);
        RecolorPiece(g, TierColor(nt));
        return g;
    }

    /// Repair the aimed piece to full, paying in proportion to the missing health.
    GameObject* Repair(Scene& s, const Vec3& origin, const Vec3& dir, float reachArg = -1.0f) {
        const float maxd = reachArg >= 0.0f ? reachArg : reach;
        RaycastHit3D hit = s.physics3D().Raycast(s, origin, dir, maxd, Owner());
        GameObject* g = ResolveStructure(hit.gameObject);
        if (!g) return nullptr;
        auto* bp = g->GetComponent<BuildPiece>();
        if (!bp || bp->FullHealth() || bp->maxHealth <= 0.0f) return nullptr;
        float missing = 1.0f - bp->health / bp->maxHealth;
        int rcost = std::max(1, (int)std::ceil(bp->cost * missing));
        if (requireResources) { Inventory* inv = Inv(); if (!inv || !inv->Remove(bp->material, rcost)) return nullptr; }
        bp->health = bp->maxHealth;
        return g;
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
    Inventory* Inv() const { GameObject* o = Owner(); return o ? o->GetComponent<Inventory>() : nullptr; }
    bool CanAfford(int tier) const {
        if (!requireResources) return true;
        Inventory* inv = Inv();
        return inv && inv->Has(TierMaterial(tier), TierCost(tier));
    }
    float CameraGap(const Vec3& camPos) const {
        GameObject* o = Owner();
        if (!o || !o->transform) return 0.0f;
        Vec3 d = camPos - o->transform->Position();
        return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    }
    /// Walk a hit collider up to the structure GameObject that owns it (doorway frame
    /// colliders are children of the piece).
    GameObject* ResolveStructure(GameObject* hitGo) const {
        for (GameObject* g = hitGo; g && g->transform; ) {
            if (g->tag == structureTag) return g;
            Transform* par = g->transform->Parent();
            g = par ? par->gameObject : nullptr;
        }
        return nullptr;
    }

    // A wall-sized frame around an opening, from child cube parts: posts + lintel for
    // a doorway (gap at the bottom — walkable), or sill + header + jambs for a window
    // (gap in the middle). AXIS-ALIGNED (no rotation): `dim` is the box footprint, and
    // its longer horizontal axis is the wall's length; `u` runs along it, `v` is up.
    void BuildFrame(Scene& s, GameObject* parent, const Vec3& dim, bool window, const Color& col) const {
        const bool  alongX = dim.x >= dim.z;
        const float L = alongX ? dim.x : dim.z;     // length along the wall
        const float T = alongX ? dim.z : dim.x;     // thickness across it
        const float H = dim.y;
        // Place a part by (along-length u, up v) with size (length lu, height lh).
        auto part = [&](float u, float v, float lu, float lh) {
            GameObject* c = s.CreateGameObject("Frame");
            if (!c) return;
            c->transform->SetParent(parent->transform, false);
            c->transform->localPosition = alongX ? Vec3{u, v, 0.0f} : Vec3{0.0f, v, u};
            c->transform->localScale    = alongX ? Vec3{lu, lh, T}   : Vec3{T, lh, lu};
            auto* mr = c->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Cube(); mr->color = col;
            if (!structureTexture.empty()) mr->texture = structureTexture;
            c->AddComponent<BoxCollider3D>();
        };
        if (!window) {
            float gapW = L * 0.45f, gapH = H * 0.72f, postW = (L - gapW) * 0.5f;
            float px = (L - postW) * 0.5f;
            part(-px, 0.0f, postW, H);                  // left post
            part( px, 0.0f, postW, H);                  // right post
            part(0.0f, gapH * 0.5f, L, H - gapH);       // lintel above the gap
        } else {
            float gapW = L * 0.5f, sill = H * 0.28f, head = H * 0.28f;
            float sideW = (L - gapW) * 0.5f, midH = H - sill - head;
            float jx = (L - sideW) * 0.5f, midV = -(H * 0.5f) + sill + midH * 0.5f;
            part(0.0f, -(H - sill) * 0.5f, L, sill);    // sill
            part(0.0f,  (H - head) * 0.5f, L, head);    // header
            part(-jx, midV, sideW, midH);               // left jamb
            part( jx, midV, sideW, midH);               // right jamb
        }
    }
    /// Recolor a piece and any frame child parts (used on upgrade).
    void RecolorPiece(GameObject* g, const Color& c) const {
        if (auto* mr = g->GetComponent<MeshRenderer>()) mr->color = c;
        if (g->transform)
            for (Transform* ch : g->transform->Children())
                if (ch && ch->gameObject)
                    if (auto* mr = ch->gameObject->GetComponent<MeshRenderer>()) mr->color = c;
    }

    void UpdatePreview(Scene& s, const Placement& p) {
        GameObject* g = EnsurePreview(s);
        if (!g || !g->transform) return;
        if (!p.show) { g->active = false; return; }
        g->active = true;
        g->transform->localPosition = p.position;
        g->transform->localRotation = Quat::Euler(0.0f, p.yaw, 0.0f);
        g->transform->localScale = {p.scale.x * 1.02f + 0.02f, p.scale.y * 1.02f + 0.02f, p.scale.z * 1.02f + 0.02f};
        if (auto* mr = g->GetComponent<MeshRenderer>())
            mr->color = p.valid ? previewFree : previewBusy;
    }
    GameObject* EnsurePreview(Scene& s) {
        if (preview_) return preview_;
        GameObject* g = s.CreateGameObject("StructurePreview");
        if (!g) return nullptr;
        g->tag = "StructurePreview";
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
