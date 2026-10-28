#pragma once
#include <cstdint>
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Scheduler.hpp"
#include "okay/Core/Tween.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Render/Color.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace okay {

class Component;
class Camera;
class IRenderer;

/// Owns a graph of GameObjects and drives their lifecycle and rendering.
/// One level / screen of the game.
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

    /// Additively MERGE another .okayscene into this one at the end of the frame,
    /// translated by `offset` — combine scenes into one seamless world without
    /// replacing what's already here. Safe to call from a script/Update; the new
    /// objects get Awake/Start on the next flush.
    void RequestMerge(const std::string& path, const Vec3& offset = Vec3{0, 0, 0}) {
        m_pendingMerges.push_back({path, offset});
    }

    // ---- Queries -------------------------------------------------------
    GameObject* Find(const std::string& name) const;
    GameObject* FindWithTag(const std::string& tag) const;
    const std::vector<std::unique_ptr<GameObject>>& Objects() const { return m_objects; }
    /// Bumped whenever objects are created or destroyed (or the scene is cleared):
    /// components that cache GameObject/Transform pointers re-resolve when it changes.
    std::uint64_t StructureVersion() const { return m_structureVersion; }
    /// Note a hierarchy change (an object created, destroyed or reparented), so
    /// caches keyed on StructureVersion() re-resolve.
    void MarkStructureChanged() { ++m_structureVersion; }
    /// Bumped whenever a component is added to or removed from any object.
    std::uint64_t ComponentVersion() const { return m_componentVersion; }
    /// A process-unique id for this scene (never reused), for caches keyed by scene.
    std::uint64_t Uid() const { return m_uid; }

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
    /// Running tweens (DOTween-style animations), ticked each frame during Update
    /// and killed with the objects they animate.
    TweenManager& tweens() { return m_tweens; }

    /// The 2D physics world, stepped each frame during Update.
    Physics2D& physics() { return m_physics; }
    const Physics2D& physics() const { return m_physics; }
    /// The 3D physics world, stepped each frame during Update.
    Physics3D& physics3D() { return m_physics3d; }
    const Physics3D& physics3D() const { return m_physics3d; }
    /// Set false to skip the physics step (e.g. for pure-UI scenes).
    bool physicsEnabled = true;
    /// With this above 0, physics runs in steps of this many seconds whatever the
    /// frame rate, so a game behaves the same at 30 and 240 FPS (frame time left
    /// over carries to the next frame). 0 = one step per frame, sized to the frame.
    /// The player, the editor's Play mode and the dedicated server use
    /// kDefaultFixedStep (scripts' FixedUpdate rate); a bare Scene keeps 0.
    float fixedTimeStep = 0.0f;
    static constexpr float kDefaultFixedStep = 1.0f / 50.0f;
    /// Fixed steps plus interpolation: how games run.
    void UseFixedStepPhysics(bool on = true) {
        fixedTimeStep = on ? kDefaultFixedStep : 0.0f;
        interpolatePhysics = on;
    }
    /// The most frame time physics catches up on in one frame, so a long hitch
    /// can't snowball into ever more steps.
    float maxPhysicsCatchUp = 1.0f / 3.0f;
    /// Draw rigidbodies part way between their last two physics poses, so motion
    /// stays smooth when the frame rate and the physics rate differ. The player
    /// and the editor's Play mode turn it on; a body moved by game code jumps
    /// there with no blending.
    bool interpolatePhysics = false;
    /// How far the current frame is between the last physics step and the next (0..1).
    float PhysicsAlpha() const { return m_physicsAlpha; }
    /// Physics steps taken during the last Update.
    int PhysicsStepsLastFrame() const { return m_physicsSteps; }
    /// Whether scripts (Script, Visual Script and Actions components) run. The
    /// editor turns it off outside Play, so opening, previewing or editing a scene
    /// never runs game code; their Start runs once it's back on.
    bool runScripts = true;

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
        // Screen-space vignette: darken the frame's edges/corners by `vignette`
        // strength (0 = off .. 1 = strong). A cheap post overlay drawn on top of the
        // finished frame, so it looks identical on every renderer backend. Great for
        // focus/mood without touching the 3D shading path.
        float vignette   = 0.0f;
        // Filmic rendering: a gamma-correct pipeline (albedo decoded to linear,
        // lighting in linear space, sRGB-encoded output) plus ACES tonemapping,
        // which rolls bright over-1.0 lighting off toward white instead of
        // hard-clipping. ON by default — this is what gives shading its depth;
        // turn off to get the legacy flat response back.
        bool  tonemap    = true;
        // ---- Skybox customization ----
        // Where the horizon band sits in the sky gradient, as a fraction of the view
        // height (0 = top, 0.5 = middle, 1 = bottom). Raise it for more sky, lower it
        // for a high horizon.
        float skyHorizonPos = 0.5f;
        // An optional sun disc painted into the sky (screen-space): position (0..1 of
        // the view), size (radius as a fraction of view height), color, and a soft
        // glow halo. Cheap and backend-independent, like the vignette.
        bool  skySun      = false;
        float skySunX     = 0.5f;
        float skySunY     = 0.30f;
        float skySunSize  = 0.05f;
        Color skySunColor = Color::FromBytes(255, 245, 214);
        // An optional star field painted in the upper sky (above the horizon band) —
        // great for night / space skies. Stars are placed deterministically from a
        // seed (so they don't flicker) via SkyStars(); density scales the count and
        // brightness scales their alpha. Cheap and backend-independent.
        bool  skyStars      = false;
        float skyStarDensity = 0.5f;    // 0..1 -> up to ~kSkyStarMax stars
        float skyStarBright  = 0.9f;    // 0..1 star alpha
        // How the ambient (indirect) light varies with surface direction:
        // 0 Flat = one colour everywhere; 1 Hemisphere = brighter on up-facing,
        // darker on down-facing surfaces (the default look); 2 Sky = up-facing
        // surfaces take the sky colours and down-facing ones the ground colour.
        enum AmbientMode { AmbientFlat = 0, AmbientHemisphere = 1, AmbientSky = 2 };
        int   ambientMode = AmbientHemisphere;
        float ambientSkyTint = 0.7f;    // Sky mode: how strongly the sky colours tint (0..1.5)
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
    /// Re-pick mainCamera from the live cameras (see PickRenderCamera). Runs at the
    /// end of every Update; call it after switching cameras outside of Update.
    void ResolveMainCamera();
    /// Issue the render pass to the given backend.
    void Render(IRenderer& renderer);
    /// The object the cursor was over at the last OnMouse* dispatch (nullptr if none).
    GameObject* MouseHoverObject() const { return m_mouseHover; }

    /// While alive, records the address of every component removed from this
    /// scene, so code holding raw component pointers across user callbacks (the
    /// physics steps) can skip the ones a callback removed. The recorded pointers
    /// are only compared, never dereferenced.
    class RemovalWatch {
    public:
        explicit RemovalWatch(Scene& scene) : m_scene(scene) { scene.m_removalWatches.push_back(&m_removed); }
        ~RemovalWatch() {
            auto& w = m_scene.m_removalWatches;
            w.erase(std::find(w.begin(), w.end(), &m_removed));
        }
        RemovalWatch(const RemovalWatch&) = delete;
        RemovalWatch& operator=(const RemovalWatch&) = delete;
        bool Gone(const Component* c) const {
            return !m_removed.empty() && std::find(m_removed.begin(), m_removed.end(), c) != m_removed.end();
        }
        bool Any() const { return !m_removed.empty(); }
    private:
        Scene& m_scene;
        std::vector<const Component*> m_removed;
    };

private:
    void StepPhysics(float deltaTime);
    float m_physicsAcc = 0.0f;     // frame time not yet simulated
    float m_physicsAlpha = 0.0f;
    int   m_physicsSteps = 0;
    friend class GameObject;
    void QueuePending(Component* component);
    void NotifyComponentRemoved(Component* component);
    void FlushPending();
    /// Pick the object under the cursor (main camera, 2D bounds) and dispatch the
    /// OnMouseEnter/Exit/Over/Down/Up/Click messages, tracking state across frames.
    void DispatchPointer();
    void DispatchActivation();   // OnEnable / OnDisable for objects switched on/off

    GameObject* m_mouseHover = nullptr;   // object the cursor was over last frame
    GameObject* m_mousePress = nullptr;   // object a press began on (for click)
    bool        m_mouseWasDown = false;   // left button state last frame

    std::string m_name;
    Scheduler   m_scheduler;
    TweenManager m_tweens;
    Physics2D   m_physics;
    Physics3D   m_physics3d;
    std::vector<std::unique_ptr<GameObject>> m_objects;
    /// Destroyed objects, stripped to their Transform. Kept until the scene is
    /// cleared so a component still pointing at a deleted bone / target / follow
    /// object reads a frozen transform instead of freed memory.
    std::vector<std::unique_ptr<GameObject>> m_graveyard;
    std::uint64_t m_structureVersion = 1;
    std::uint64_t m_componentVersion = 1;
    std::uint64_t m_uid = 0;
    bool m_hasOrdered = false;   // some component has a non-zero ExecutionOrder
    std::vector<Component*>  m_pending;   // awaiting Awake/Start
    std::vector<Component*>  m_active;    // receive Update each frame
    std::vector<GameObject*> m_destroyQueue;
    std::string m_pendingLoad;
    bool        m_hasPendingLoad = false;
    std::vector<std::pair<std::string, Vec3>> m_pendingMerges;   // deferred additive scene merges
    std::vector<std::vector<const Component*>*> m_removalWatches;   // live RemovalWatch lists
};

/// True when `go`'s scene lets scripts run (see Scene::runScripts).
inline bool ScriptsRun(const GameObject* go) {
    const Scene* s = go ? go->scene() : nullptr;
    return !s || s->runScripts;
}

} // namespace okay
