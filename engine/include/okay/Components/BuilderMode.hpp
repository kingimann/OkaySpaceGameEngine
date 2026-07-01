#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/SceneSerializer.hpp"
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

/// In-game Builder Mode — an editor you operate as a walking player. Aim at a
/// surface and left-click to drop a primitive shape (cube, sphere, cylinder, …)
/// or stamp a saved prefab; right-click removes the piece you're aiming at. You
/// can rotate and scale the brush before placing, grab a placed piece to nudge it,
/// and — once a model looks right — save the whole assembly to a `.okayprefab`
/// file with one key. Two workflows share the same tool:
///   • Model an object: parts parent under one "model" root, then Save (key P)
///     writes that root and its children as a reusable prefab.
///   • Build a map: switch the brush to Prefab and stamp instances of any saved
///     prefab around the world while you walk.
/// Like the other builders it casts from the scene's main camera (centre-screen,
/// down the real look axis), compensates for a third-person camera gap so reach is
/// measured from the player, and ignores your own body. Placed pieces are real
/// GameObjects (mesh + box collider) tagged `buildTag`, so they save with the scene.
class BuilderMode : public Behaviour {
public:
    /// What left-click places. The first entries are primitive meshes; `Prefab`
    /// stamps an instance of `prefabPath` instead.
    enum class Brush { Cube = 0, Sphere = 1, Cylinder = 2, Cone = 3, Wedge = 4,
                       Pyramid = 5, Plane = 6, Capsule = 7, Torus = 8, Prefab = 9 };
    static constexpr int kBrushCount = 10;

    Brush       brush         = Brush::Cube;
    float       gridSize      = 1.0f;        ///< snap step (world units)
    bool        snapToGrid    = true;        ///< round placement to the grid
    float       reach         = 8.0f;        ///< max place/remove distance from the player
    float       rotateStepDeg = 15.0f;       ///< yaw added per rotate press
    float       scaleStep     = 0.1f;        ///< brush scale change per scale press
    float       minScale      = 0.1f;
    float       maxScale      = 20.0f;
    Color       partColor     = Color::FromBytes(170, 172, 182);
    std::string partTexture;                 ///< optional texture for placed primitives
    std::string buildTag      = "BuildPart"; ///< tag placed pieces carry (only these are removable)
    std::string prefabPath;                  ///< file the Prefab brush stamps / default Save target
    bool        parentToModel = true;        ///< place primitives under one model root (savable as a prefab)
    std::string modelName     = "BuildModel";///< name of that root

    // ---- Controls ----
    int  placeButton  = 0;                   ///< mouse button to place (0 = left)
    int  removeButton = 1;                   ///< mouse button to remove (1 = right)
    char nextBrushKey = ']';
    char prevBrushKey = '[';
    char rotateKey    = 'r';                 ///< yaw the brush (and a grabbed piece)
    char scaleUpKey   = '=';
    char scaleDownKey = '-';
    char grabKey      = 'g';                 ///< pick up / drop the piece you're aiming at
    char saveKey      = 'p';                 ///< save the assembled model as a prefab
    char saveMapKey   = 'o';                 ///< save the whole scene (the built map) to disk
    std::string mapPath;                     ///< where SaveMap writes (default "built_map.okayscene")
    bool brushHotkeys = true;                ///< keys 1–9 select primitive brushes
    bool showPreview   = true;               ///< ghost of the next placement
    bool showCrosshair = true;               ///< auto-add an aim reticle
    Color previewFree  = Color::FromBytes(80, 255, 120, 230);
    Color previewBusy  = Color::FromBytes(255, 80, 80, 230);

    void Update(float) override {
        if (!gameObject) return;
        Scene* s = GetScene();
        if (!s || !s->mainCamera || !s->mainCamera->gameObject || !s->mainCamera->gameObject->transform) return;
        Transform* cam = s->mainCamera->gameObject->transform;
        const Vec3 origin = cam->Position(), dir = cam->Forward() * -1.0f;   // cameras look down -Z
        const float r = reach + CameraGap(origin);

        if (showCrosshair && !crosshairChecked_) { crosshairChecked_ = true; EnsureCrosshair(*s); }

        // ---- Brush / transform hotkeys ----
        if (brushHotkeys) {
            for (int i = 0; i < kBrushCount - 1; ++i)        // 1..9 → primitive brushes
                if (Input::GetKeyDown(char('1' + i))) brush = (Brush)i;
        }
        if (nextBrushKey && Input::GetKeyDown(nextBrushKey)) CycleBrush(+1);
        if (prevBrushKey && Input::GetKeyDown(prevBrushKey)) CycleBrush(-1);
        if (rotateKey   && Input::GetKeyDown(rotateKey))   yaw_ = std::fmod(yaw_ + rotateStepDeg, 360.0f);
        if (scaleUpKey   && Input::GetKeyDown(scaleUpKey))   scale_ = Clampf(scale_ + scaleStep, minScale, maxScale);
        if (scaleDownKey && Input::GetKeyDown(scaleDownKey)) scale_ = Clampf(scale_ - scaleStep, minScale, maxScale);
        if (saveKey      && Input::GetKeyDown(saveKey))      SaveModel();
        if (saveMapKey   && Input::GetKeyDown(saveMapKey))   SaveMap();

        // ---- Grab: pick up the aimed piece, it tracks the aim until dropped ----
        if (grabKey && Input::GetKeyDown(grabKey)) ToggleGrab(*s, origin, dir, r);
        if (grabbed_) {
            if (grabbed_->transform) {
                grabbed_->transform->localPosition = AimPoint(*s, origin, dir, r, grabbed_);
                grabbed_->transform->localRotation = Quat::Euler(0.0f, yaw_, 0.0f);
            }
            if (preview_) preview_->active = false;
            // A click (or grab key, handled above) drops it in place.
            if (Input::GetMouseButtonDown(placeButton)) grabbed_ = nullptr;
            return;
        }

        if (showPreview) UpdatePreview(*s, origin, dir, r);
        else if (preview_) preview_->active = false;

        if (Input::GetMouseButtonDown(placeButton))  PlaceAt(*s, origin, dir, r);
        else if (Input::GetMouseButtonDown(removeButton)) RemoveAt(*s, origin, dir, r);
    }

    // ---- Camera-independent actions (so they're unit-testable) --------------

    /// Place the current brush along a ray. Returns the new piece (or instanced
    /// prefab root), or nullptr if there's nothing to build against.
    GameObject* PlaceAt(Scene& s, const Vec3& origin, const Vec3& dir, float reachArg = -1.0f) {
        const float r = reachArg >= 0.0f ? reachArg : reach;
        Vec3 pos; if (!ResolvePlacement(s, origin, dir, r, nullptr, pos)) return nullptr;
        if (brush == Brush::Prefab) return StampPrefab(s, pos, prefabPath);
        return PlacePrimitive(s, pos);
    }

    /// Remove the build piece you're aiming at (only ones this tool placed). For a
    /// piece that's part of a model, the whole model part under the cursor is removed.
    GameObject* RemoveAt(Scene& s, const Vec3& origin, const Vec3& dir, float reachArg = -1.0f) {
        const float r = reachArg >= 0.0f ? reachArg : reach;
        RaycastHit3D hit = s.physics3D().Raycast(s, origin, dir, r, Owner());
        GameObject* g = ResolveBuildPiece(hit.gameObject);
        if (!g) return nullptr;
        s.Destroy(g);
        return g;
    }

    /// Stamp an instance of a prefab file at a world point (the map-building brush).
    GameObject* StampPrefab(Scene& s, const Vec3& pos, const std::string& path) {
        if (path.empty()) return nullptr;
        GameObject* g = SceneSerializer::InstantiateFromFile(s, path, nullptr);
        if (!g) return nullptr;
        if (g->transform) {
            g->transform->localPosition = pos;
            g->transform->localRotation = Quat::Euler(0.0f, yaw_, 0.0f) * g->transform->localRotation;
        }
        // Tag the stamped root so it's removable with right-click like other pieces.
        if (g->tag.empty()) g->tag = buildTag;
        return g;
    }

    /// Save the assembled model (its root + all placed parts) to a `.okayprefab`
    /// file. Returns false if there's nothing built yet or the write fails. With no
    /// `path`, uses `prefabPath`, falling back to `<modelName>.okayprefab`.
    bool SaveModel(const std::string& path = "") {
        if (!model_ || !model_->transform || model_->transform->Children().empty()) return false;
        std::string out = !path.empty() ? path
                        : (!prefabPath.empty() ? prefabPath : modelName + ".okayprefab");
        return SceneSerializer::SaveObjectToFile(*model_, out);
    }

    /// Save the WHOLE current scene (the built map + everything in it) to a
    /// `.okayscene` file, so a map you build while playing persists and can be
    /// reloaded. Runs in Play too (BuilderMode updates like any component), so a
    /// shipped game can let players build and save. With no `path`, uses `mapPath`,
    /// falling back to "built_map.okayscene".
    bool SaveMap(const std::string& path = "") {
        Scene* s = GetScene();
        if (!s) return false;
        std::string out = !path.empty() ? path
                        : (!mapPath.empty() ? mapPath : "built_map.okayscene");
        return SceneSerializer::SaveToFile(*s, out);
    }

    /// Grid-snap a world point to the nearest grid node (no snap if snapToGrid off
    /// or gridSize ~0). Exposed for tests.
    Vec3 Snap(const Vec3& p) const {
        if (!snapToGrid) return p;
        float g = gridSize > 1e-4f ? gridSize : 1.0f;
        return Vec3{std::round(p.x / g) * g, std::round(p.y / g) * g, std::round(p.z / g) * g};
    }

    /// The model root all parts are parented under (created on first need). Null
    /// until something is placed in model mode.
    GameObject* ModelRoot() const { return model_; }
    float CurrentYaw()   const { return yaw_; }
    float CurrentScale() const { return scale_; }

    static Mesh MeshFor(Brush b) {
        switch (b) {
            case Brush::Sphere:   return Mesh::Sphere();
            case Brush::Cylinder: return Mesh::Cylinder();
            case Brush::Cone:     return Mesh::Cone();
            case Brush::Wedge:    return Mesh::Wedge();
            case Brush::Pyramid:  return Mesh::Pyramid();
            case Brush::Plane:    return Mesh::Plane(1.0f);
            case Brush::Capsule:  return Mesh::Capsule();
            case Brush::Torus:    return Mesh::Torus();
            case Brush::Cube:
            default:              return Mesh::Cube();
        }
    }
    static const char* BrushName(Brush b) {
        switch (b) {
            case Brush::Cube: return "Cube"; case Brush::Sphere: return "Sphere";
            case Brush::Cylinder: return "Cylinder"; case Brush::Cone: return "Cone";
            case Brush::Wedge: return "Wedge"; case Brush::Pyramid: return "Pyramid";
            case Brush::Plane: return "Plane"; case Brush::Capsule: return "Capsule";
            case Brush::Torus: return "Torus"; case Brush::Prefab: return "Prefab";
        }
        return "Cube";
    }

private:
    GameObject* preview_ = nullptr;   ///< runtime-only ghost (not saved/removable)
    GameObject* model_   = nullptr;   ///< root the assembled object's parts hang under
    GameObject* grabbed_ = nullptr;   ///< piece currently being nudged
    Brush       previewBrush_ = Brush::Cube;   ///< mesh the ghost currently shows
    bool        crosshairChecked_ = false;
    float       yaw_   = 0.0f;
    float       scale_ = 1.0f;

    static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    void CycleBrush(int d) {
        int n = ((int)brush + d) % kBrushCount;
        if (n < 0) n += kBrushCount;
        brush = (Brush)n;
    }

    /// The player carrying this builder: the root ancestor of the object it sits on,
    /// so the aim ray skips the whole body and reach is measured from the player.
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

    /// Where a placement lands: the snapped point on the surface you're aiming at
    /// (lifted so a solid rests on it), or arm's length if you're aiming at nothing.
    /// `ignore` is excluded from the cast (used when re-aiming a grabbed piece).
    bool ResolvePlacement(Scene& s, const Vec3& origin, const Vec3& dir, float r,
                          GameObject* ignore, Vec3& outPos) {
        outPos = AimPoint(s, origin, dir, r, ignore, /*requireHit=*/false);
        return true;   // Builder mode is permissive — placing in open air is allowed.
    }
    Vec3 AimPoint(Scene& s, const Vec3& origin, const Vec3& dir, float r,
                  GameObject* ignore, bool /*requireHit*/ = false) {
        RaycastHit3D hit = s.physics3D().Raycast(s, origin, dir, r, ignore ? ignore : Owner());
        Vec3 target;
        if (hit.hit) {
            float lift = (brush == Brush::Plane) ? 0.0f : 0.5f * scale_;
            target = hit.point + hit.normal * lift;
        } else {
            target = origin + dir * r;
        }
        return Snap(target);
    }

    GameObject* PlacePrimitive(Scene& s, const Vec3& pos) {
        GameObject* g = s.CreateGameObject(BrushName(brush));
        if (!g) return nullptr;
        g->tag = buildTag;
        if (parentToModel) {
            GameObject* root = EnsureModel(s);
            if (root && root->transform) g->transform->SetParent(root->transform, false);
        }
        g->transform->localPosition = pos;
        g->transform->localRotation = Quat::Euler(0.0f, yaw_, 0.0f);
        g->transform->localScale = {scale_, scale_, scale_};
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh = MeshFor(brush);
        mr->color = partColor;
        if (!partTexture.empty()) mr->texture = partTexture;
        g->AddComponent<BoxCollider3D>();
        return g;
    }

    GameObject* EnsureModel(Scene& s) {
        if (model_) return model_;
        // Re-use an existing root by name if the scene already has one (e.g. loaded).
        if (GameObject* found = s.Find(modelName)) { model_ = found; return model_; }
        model_ = s.CreateGameObject(modelName);
        return model_;
    }

    /// Pick up the aimed piece (or drop the one we hold). A grabbed piece follows
    /// the aim each frame and is dropped by a click or another grab press.
    void ToggleGrab(Scene& s, const Vec3& origin, const Vec3& dir, float r) {
        if (grabbed_) { grabbed_ = nullptr; return; }
        RaycastHit3D hit = s.physics3D().Raycast(s, origin, dir, r, Owner());
        grabbed_ = ResolveBuildPiece(hit.gameObject);
        if (grabbed_ && grabbed_->transform)
            yaw_ = grabbed_->transform->localRotation.ToEuler().y;
    }

    /// Walk a hit collider up to the build piece that owns it. A model part is the
    /// direct child of the model root; standalone pieces are the hit object itself.
    GameObject* ResolveBuildPiece(GameObject* hitGo) const {
        for (GameObject* g = hitGo; g && g->transform; ) {
            if (g->tag == buildTag) return g;
            Transform* par = g->transform->Parent();
            g = par ? par->gameObject : nullptr;
        }
        return nullptr;
    }

    void UpdatePreview(Scene& s, const Vec3& origin, const Vec3& dir, float r) {
        Vec3 pos = AimPoint(s, origin, dir, r, nullptr);
        GameObject* p = EnsurePreview(s);
        if (!p || !p->transform) return;
        // The Prefab brush has no fixed shape; show a unit cube marker for it.
        Brush shape = (brush == Brush::Prefab) ? Brush::Cube : brush;
        if (shape != previewBrush_) {
            previewBrush_ = shape;
            if (auto* mr = p->GetComponent<MeshRenderer>()) mr->mesh = MeshFor(shape);
        }
        p->active = true;
        p->transform->localPosition = pos;
        p->transform->localRotation = Quat::Euler(0.0f, yaw_, 0.0f);
        float e = scale_ * 1.02f + 0.01f;
        p->transform->localScale = {e, e, e};
        if (auto* mr = p->GetComponent<MeshRenderer>())
            mr->color = (brush == Brush::Prefab && prefabPath.empty()) ? previewBusy : previewFree;
    }
    GameObject* EnsurePreview(Scene& s) {
        if (preview_) return preview_;
        GameObject* p = s.CreateGameObject("BuildPreview");
        if (!p) return nullptr;
        p->tag = "BuildPreview";   // NOT buildTag → never removable or saved with a model
        auto* mr = p->AddComponent<MeshRenderer>();
        mr->mesh = MeshFor(previewBrush_);
        mr->wireframe = true;
        mr->unlit = true;
        mr->shader = MeshRenderer::Shader::Unlit;
        mr->color = previewFree;
        preview_ = p;
        return p;
    }
    void EnsureCrosshair(Scene& s) {
        for (const auto& up : s.Objects())
            if (up && up->GetComponent<Crosshair>()) return;
        if (gameObject && !gameObject->GetComponent<Crosshair>())
            gameObject->AddComponent<Crosshair>()->dot = true;
    }
};

} // namespace okay
