#pragma once
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Scheduler.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Render/Color.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay {

class Component;
class Camera;
class IRenderer;

/// Owns a graph of GameObjects and drives their lifecycle and rendering.
/// Analogous to a Unity Scene.
class Scene {
public:
    explicit Scene(std::string name = "Scene");
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    const std::string& Name() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    /// Create an empty GameObject (with a Transform) owned by this scene.
    GameObject* CreateGameObject(const std::string& name = "GameObject");

    /// Remove every GameObject immediately (used when loading a scene).
    void Clear();

    /// Clone a prefab (a GameObject and its descendants) into this scene.
    GameObject* Instantiate(const GameObject& prefab);
    GameObject* Instantiate(const GameObject& prefab, const Vec3& position);

    /// Mark a GameObject for destruction; removed at the end of the frame.
    void Destroy(GameObject* go);
    /// Process the pending-destroy queue now (runs OnDestroy + frees the objects).
    /// Update() calls this each frame; the editor calls it in edit mode so deferred
    /// destroys (e.g. removing a rig) take effect without a full simulation tick.
    void FlushDestroyed();

    /// Request loading a .okayscene file at the end of the current frame
    /// (safe to call from a script/Update). Replaces this scene's contents.
    void RequestLoad(const std::string& path) { m_pendingLoad = path; m_hasPendingLoad = true; }
    bool HasPendingLoad() const { return m_hasPendingLoad; }

    // ---- Queries -------------------------------------------------------
    GameObject* Find(const std::string& name) const;
    GameObject* FindWithTag(const std::string& tag) const;
    const std::vector<std::unique_ptr<GameObject>>& Objects() const { return m_objects; }

    /// Layer a widget frontmost/backmost. UI draws in hierarchy order, so a parented
    /// object moves to the last/first slot among its SIBLINGS (last = on top); a root
    /// object moves to the end/start of the scene's object list. Used for UI layering.
    void MoveToFront(GameObject* go);
    void MoveToBack(GameObject* go);
    /// Reorder `go` among its siblings (same parent) by `dir` (-1 = up/earlier,
    /// +1 = down/later) — drives hierarchy reordering and UI draw order.
    void MoveSibling(GameObject* go, int dir);
    /// Move `go` to the first (toFront) or last sibling position.
    void MoveSiblingToEdge(GameObject* go, bool toFront);
    /// Make `go` a sibling of `anchor` placed just before/after it (drag-reorder).
    void ReorderSibling(GameObject* go, GameObject* anchor, bool after);

    /// Collect every component of type T across all GameObjects in the scene.
    template <typename T>
    std::vector<T*> FindObjectsOfType() const {
        std::vector<T*> out;
        out.reserve(m_objects.size());   // typically ~1 of a given type per object
        for (const auto& go : m_objects)
            go->template CollectComponents<T>(out);   // append directly — no per-object temp vector
        return out;
    }
    /// The first component of type T found in the scene, or nullptr.
    template <typename T>
    T* FindObjectOfType() const {
        for (const auto& go : m_objects)
            if (T* c = go->GetComponent<T>()) return c;
        return nullptr;
    }

    /// The camera used for rendering. Set automatically by Camera::Awake.
    Camera* mainCamera = nullptr;

    /// Time-based callbacks (Invoke / InvokeRepeating / Tween), ticked each
    /// frame during Update.
    Scheduler& scheduler() { return m_scheduler; }

    /// The 2D physics world, stepped each frame during Update.
    Physics2D& physics() { return m_physics; }
    const Physics2D& physics() const { return m_physics; }
    /// The 3D physics world, stepped each frame during Update.
    Physics3D& physics3D() { return m_physics3d; }
    const Physics3D& physics3D() const { return m_physics3d; }
    /// Set false to skip the physics step (e.g. for pure-UI scenes).
    bool physicsEnabled = true;

    /// Per-scene rendering settings, saved with the scene so a built game shows
    /// the same sky and base lighting the editor previews (skybox + ambient).
    struct RenderSettings {
        bool  skybox     = true;
        Color skyTop     = Color::FromBytes(70, 120, 200);   // zenith
        Color skyHorizon = Color::FromBytes(150, 185, 225);  // horizon haze
        Color skyBottom  = Color::FromBytes(120, 120, 130);  // ground-ish
        float ambient    = 0.15f;                            // base light (0..1)
        // Distance fog: fades distant geometry toward `fogColor` between
        // `fogStart` and `fogEnd` world units from the camera (depth cue + it
        // hides the far clip / pop-in). Off by default.
        bool  fog        = false;
        Color fogColor   = Color::FromBytes(150, 185, 225);  // default = horizon
        float fogStart   = 20.0f;
        float fogEnd     = 90.0f;
    };
    RenderSettings renderSettings;

    /// Default TTF/OTF font for this scene's UI widgets (buttons, dropdowns, tabs,
    /// toggles, sliders, input fields, tooltips, ...). Empty = the built-in 8x8
    /// bitmap font. A widget/Text with its own fontPath overrides this. Travels with
    /// the scene so the editor preview and the shipped player match.
    std::string uiFont;

    // ---- Lifecycle (driven by the Application) ------------------------
    /// Run Awake/Start on all components created so far.
    void Start();
    /// Advance one frame: flush pending components, Update, LateUpdate, cleanup.
    void Update(float deltaTime);
    /// Issue the render pass to the given backend.
    void Render(IRenderer& renderer);

private:
    friend class GameObject;
    void QueuePending(Component* component);
    void NotifyComponentRemoved(Component* component);
    void FlushPending();
    /// Pick the object under the cursor (main camera, 2D bounds) and dispatch the
    /// OnMouseEnter/Exit/Over/Down/Up/Click messages, tracking state across frames.
    void DispatchPointer();

    GameObject* m_mouseHover = nullptr;   // object the cursor was over last frame
    GameObject* m_mousePress = nullptr;   // object a press began on (for click)
    bool        m_mouseWasDown = false;   // left button state last frame

    std::string m_name;
    Scheduler   m_scheduler;
    Physics2D   m_physics;
    Physics3D   m_physics3d;
    std::vector<std::unique_ptr<GameObject>> m_objects;
    std::vector<Component*>  m_pending;   // awaiting Awake/Start
    std::vector<Component*>  m_active;    // receive Update each frame
    std::vector<GameObject*> m_destroyQueue;
    std::string m_pendingLoad;
    bool        m_hasPendingLoad = false;
};

} // namespace okay
