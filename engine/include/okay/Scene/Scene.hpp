#pragma once
#include <cstdint>
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Core/Scheduler.hpp"
#include "okay/Core/Tween.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Bounds.hpp"
#include "okay/Scene/SceneValidation.hpp"
#include <algorithm>
#include <memory>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace okay {

class Component;
class SceneLoadJob;
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

    /// The live object with this persistent id (GameObject::Guid), or nullptr.
    GameObject* FindByGuid(const std::string& guid) const;

    /// Remove every GameObject immediately (used when loading a scene). Components that
    /// are live get OnDisable / OnDestroy first, and the 3D physics world and its gravity
    /// go back to their defaults, like everything else the old scene held. The destructor
    /// does the same OnDestroy pass.
    void Clear();

    /// Clone a prefab (a GameObject and its descendants) into this scene. The clone is a root
    /// object with the original's LOCAL pose, whatever the original's parent was (Duplicate
    /// keeps the parent). Components, tags, layers and the hierarchy below it are copied;
    /// the clone gets its own instance id and guid.
    GameObject* Instantiate(const GameObject& prefab);
    GameObject* Instantiate(const GameObject& prefab, const Vec3& position);
    /// A root clone at a world position and rotation (scale as the original's).
    GameObject* Instantiate(const GameObject& prefab, const Vec3& position, const Quat& rotation);
    /// A clone under `parent` (nullptr = a root). `worldPositionStays` false keeps the original's
    /// local pose relative to the new parent; true keeps the pose it would have as a root. A
    /// parent that is refused (destroyed, or in another scene) leaves the clone a root.
    GameObject* Instantiate(const GameObject& prefab, Transform* parent, bool worldPositionStays = false);
    /// A clone at a WORLD position and rotation under `parent` (nullptr = a root), like a bullet
    /// leaving a muzzle that is itself moving.
    GameObject* Instantiate(const GameObject& prefab, const Vec3& position, const Quat& rotation, Transform* parent);
    /// A copy of `original` and everything below it, under the same parent with the same local
    /// pose, placed right after it among its siblings, named with UniqueName so "Crate" becomes
    /// "Crate (1)". nullptr when the original is destroyed.
    GameObject* Duplicate(const GameObject& original);
    /// `base` if no sibling already has that name, else "base (1)", "base (2)" ... (the first
    /// free number; a trailing " (n)" on `base` counts from its stem, so "Crate (1)" gives
    /// "Crate (2)"). Siblings are the children of `parent`, or the root objects when it is
    /// null. An empty base reads as "GameObject".
    std::string UniqueName(const std::string& base, const Transform* parent = nullptr) const;

    /// Mark a GameObject for destruction; removed at the end of the frame. Until
    /// then it keeps its place in the hierarchy (and so its world pose), and its
    /// OnDestroy still sees its parent.
    void Destroy(GameObject* go);
    /// Destroy `go` after `seconds` of this scene's time (the Update deltas).
    /// Destroying it earlier cancels this; asking again keeps the sooner time.
    /// Zero, less or NaN destroys it like Destroy(go).
    void Destroy(GameObject* go, float seconds);
    /// Destroy `go` and everything below it right now (OnDestroy runs, the objects are freed),
    /// together with anything else already queued by Destroy. From inside an Update, a physics
    /// callback or an OnDestroy the scene is walking its component lists, so there it does what
    /// Destroy does and the objects go at the end of the frame. Meant for tools, edit mode and
    /// tests; game code should use Destroy.
    void DestroyImmediate(GameObject* go);
    /// Keep `go` and its children when this scene is replaced by RequestLoad (not
    /// by RequestMerge), with the same objects and instance ids. A kept object that
    /// still has a parent outside the kept set moves to the root, keeping its pose.
    /// Timers, tweens and physics contacts of the old scene do not carry over.
    void DontDestroyOnLoad(GameObject* go);
    bool IsDontDestroyOnLoad(const GameObject* go) const;
    /// Process the pending-destroy queue now (runs OnDestroy + frees the objects).
    /// Update() calls this each frame; the editor calls it in edit mode so deferred
    /// destroys (e.g. removing a rig) take effect without a full simulation tick.
    void FlushDestroyed();

    /// Request loading a .okayscene file at the end of the current frame
    /// (safe to call from a script/Update). Replaces this scene's contents.
    /// The last load requested in a frame wins (a text load requested before it is
    /// dropped). `done`, if given, is told whether the file loaded (false too when
    /// a later request replaced this one).
    void RequestLoad(const std::string& path, std::function<void(bool)> done = {}) {
        if (m_pendingLoadDone) m_pendingLoadDone(false);   // the earlier request never ran
        m_pendingLoad = path; m_hasPendingLoad = true; m_pendingLoadDone = std::move(done);
        m_hasPendingText = false; m_pendingText.clear(); m_pendingTextAfter = {};
    }
    bool HasPendingLoad() const { return m_hasPendingLoad; }
    /// Why the last RequestLoad / RequestLoadText applied by Update failed (it was also
    /// printed to stderr); empty after a load that worked. The scene is untouched by a failed load.
    const std::string& LastLoadError() const { return m_lastLoadError; }
    /// Like RequestLoad, from serialized scene text (a save game) instead of a
    /// file; `after` runs once the loaded objects have started.
    void RequestLoadText(std::string text, std::function<void(Scene&)> after = {}) {
        if (m_pendingLoadDone) { auto d = std::move(m_pendingLoadDone); m_pendingLoadDone = {}; d(false); }
        m_pendingText = std::move(text); m_pendingTextAfter = std::move(after);
        m_hasPendingText = true; m_hasPendingLoad = false; m_pendingLoad.clear();
    }

    /// Additively MERGE another .okayscene into this one at the end of the frame,
    /// translated by `offset` — combine scenes into one seamless world without
    /// replacing what's already here. Safe to call from a script/Update; the new
    /// objects get Awake/Start on the next flush.
    void RequestMerge(const std::string& path, const Vec3& offset = Vec3{0, 0, 0}) {
        m_pendingMerges.push_back({path, offset});
    }

    // ---- Incremental scene loads ----------------------------------------
    /// How BeginAsyncLoad brings a scene in.
    struct AsyncLoadOptions {
        /// true: merge into what is here (see RequestMerge); false: replace this scene's
        /// contents, DontDestroyOnLoad objects excepted (as RequestLoad does).
        bool  additive = false;
        Vec3  offset{0, 0, 0};          // additive: moves the new top-level objects
        int   objectsPerFrame = 64;    // GameObjects created per Update
        std::function<void(float)> onProgress;   // 0..1, after each step
        std::function<void(bool)>  onDone;       // once: true = loaded, false = failed / cancelled
    };
    /// Start loading a .okayscene over several frames: the file is read now and
    /// `objectsPerFrame` objects are made at the end of each Update, so a loading
    /// screen keeps running. Nothing in the new content Awakes or Starts until all of it
    /// is in. Returns a handle (> 0), or 0 if the file could not be read (onDone(false)
    /// has then run). RequestLoad / RequestMerge are unchanged and still load in one go.
    int   BeginAsyncLoad(const std::string& path, AsyncLoadOptions opts);
    int   BeginAsyncLoad(const std::string& path) { return BeginAsyncLoad(path, AsyncLoadOptions()); }
    bool  IsLoading(int handle) const;
    /// Finished without error (and not unloaded yet).
    bool  IsLoaded(int handle) const;
    bool  AnyLoading() const;
    /// 0..1 while loading, 1 once loaded, 0 for an unknown / failed handle.
    float LoadProgress(int handle) const;
    /// Remove what that load brought in (destroyed at the end of the frame), or stop it
    /// half way and remove what it made. False for an unknown handle. For a replacing
    /// (non-additive) load this does not bring the old scene back.
    bool  UnloadAdditive(int handle);

    // ---- Queries -------------------------------------------------------
    GameObject* Find(const std::string& name) const;
    /// The first object (in scene order) carrying `tag`; an empty tag matches nothing.
    /// Served from a tag index (rebuilt only when objects or tags changed).
    GameObject* FindWithTag(const std::string& tag) const;
    /// Every object carrying `tag`, in scene order.
    std::vector<GameObject*> FindGameObjectsWithTag(const std::string& tag) const;
    /// How many objects carry `tag` (0 for the empty tag).
    int CountWithTag(const std::string& tag) const;
    /// Every object called exactly `name`, in scene order.
    std::vector<GameObject*> FindGameObjectsWithName(const std::string& name) const;
    /// Every object on `layer` (0-31; anything else matches nothing), in scene order, and how many.
    std::vector<GameObject*> FindGameObjectsOnLayer(int layer) const;
    int CountOnLayer(int layer) const;
    /// The tags objects carry that are not in the project tag list (Tags::All), sorted, each once.
    std::vector<std::string> UnlistedTags() const;
    /// Give every object tagged `from` the tag `to` ("" removes the tag). Returns how many changed;
    /// 0 and nothing changed when `from` is empty or `to` is not accepted (Tags::Accepts).
    int ReplaceTag(const std::string& from, const std::string& to);
    /// The world-space box around the renderers (mesh and sprite) of `root` and, with
    /// `includeChildren`, everything below it. Inactive objects (with their subtrees) and disabled
    /// renderers are left out. Empty (Bounds::IsEmpty) when nothing draws. Reads every mesh
    /// vertex: cache the result rather than calling it every frame on a big model.
    static Bounds WorldBounds(const GameObject& root, bool includeChildren = true);

    // ---- Layer collision matrix (shared by 2D and 3D physics) ----------
    /// Whether colliders on layers a and b interact (all do by default). Applies to
    /// both physics worlds, is saved with the scene, and resets on Clear().
    void SetLayerCollision(int a, int b, bool enabled);
    bool LayersCollide(int a, int b) const { return m_physics.LayersCollide(a, b); }
    /// Bit b set = layer b collides with `layer`: a ready layer mask for a physics query.
    std::uint32_t LayerCollisionMask(int layer) const { return m_physics.CollisionMask(layer); }
    /// Every layer collides with every other again.
    void ResetLayerCollisions();
    bool HasCustomLayerCollisions() const;
    /// Ready-made matrices. Everything = the default; Nothing = no pair collides (not even a layer
    /// with itself); SameLayerOnly = a layer collides with itself and with no other.
    enum class LayerCollisionPreset { Everything, Nothing, SameLayerOnly };
    void ApplyLayerCollisionPreset(LayerCollisionPreset preset);
    /// Set every pair (a, b) with layer a in `maskA` and layer b in `maskB` (bit n = layer n).
    void SetLayerCollisionGroups(std::uint32_t maskA, std::uint32_t maskB, bool enabled);
    /// SetLayerCollision with layer names (Layers::NameToLayer). False, nothing changed, when a name is unknown.
    bool SetLayerCollisionByName(const std::string& a, const std::string& b, bool enabled);
    /// The pairs that do not collide, each once with first <= second (a layer that ignores itself is (n, n)).
    std::vector<std::pair<int, int>> DisabledLayerPairs() const;
    /// Look an object up by hierarchy path: "Parent/Child/Leaf" starts at a root object
    /// called "Parent" and follows child names (the first match at each level). A leading
    /// '/' is allowed. Find() also accepts a path when no object is literally named so.
    GameObject* FindByPath(const std::string& path) const;
    /// The objects with no parent, in scene order (the top level of the hierarchy).
    std::vector<GameObject*> Roots() const;
    /// Check this scene for broken things (see SceneSerializer::Validate): component
    /// records and fields it cannot read, missing asset files, broken parent links and
    /// duplicate guids. Works on the scene's saved form, so runtime-only objects are not covered.
    SceneValidationReport Validate(const std::string& assetRoot = "") const;
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

        // ---- Fog modes (extend the distance fog above) ----
        // Linear uses fogStart/fogEnd. Exponential thickens with `fogDensity` per
        // unit of distance. Height adds a layer that thins above `fogHeight` by
        // `fogFalloff` per unit of altitude (valley mist, a low morning fog).
        // fogMatchSky takes the fog colour from the sky's horizon, so distant
        // geometry dissolves into the sky instead of a flat band.
        enum FogMode { FogLinear = 0, FogExponential = 1, FogHeight = 2 };
        int   fogMode     = FogLinear;
        float fogDensity  = 0.02f;
        float fogHeight   = 0.0f;
        float fogFalloff  = 0.15f;
        bool  fogMatchSky = false;

        // ---- Sky mode ----
        // Colour = the gradient above (painted screen-space by the editor / player).
        // Procedural = a sun-driven sky drawn by the GPU renderers behind
        // everything: colours follow the sun's height (the scene's directional
        // light, so a DayNightCycle drives it), a sun disc with a halo, horizon
        // haze, stars at night (skyStarDensity / skyStarBright) and a cloud layer.
        enum SkyMode { SkyColour = 0, SkyProcedural = 1 };
        int   skyMode      = SkyColour;
        float skySunDisc   = 2.5f;    // sun disc angular radius, degrees
        float skySunHalo   = 0.6f;    // 0..1 halo / glow around the sun
        float skyHaze      = 0.5f;    // 0..1 horizon haze brightness
        float cloudCover   = 0.0f;    // 0 = clear .. 1 = overcast
        float cloudDensity = 0.6f;    // 0..1 opacity of the cloud layer
        float cloudSpeed   = 1.0f;    // drift speed multiplier
        float cloudScale   = 1.0f;    // noise scale (bigger = larger clouds)

        // ---- Post-processing (GPU renderers; the software renderer keeps its
        // own output). With everything off / neutral the frame is untouched. ----
        struct PostSettings {
            float exposure = 1.0f;                                   // scene brightness multiplier
            enum Tonemap { TonemapNone = 0, TonemapACES = 1, TonemapFilmic = 2 };
            int   tonemap = TonemapACES;                             // curve for the linear pipeline
            bool  bloom = false;
            float bloomThreshold = 1.0f, bloomIntensity = 0.6f, bloomRadius = 1.0f;
            bool  grading = false;
            float temperature = 0.0f, tint = 0.0f;                  // -1..1 white balance
            float saturation = 1.0f, contrast = 1.0f;
            Vec3  lift{0, 0, 0}, gamma{0, 0, 0}, gain{0, 0, 0};     // -1..1 per channel (0 = neutral)
            float vignetteSmooth = 0.5f;                             // intensity is RenderSettings::vignette
            bool  fxaa = false;
            bool  ssao = false;
            float ssaoRadius = 0.5f, ssaoIntensity = 0.8f;
            bool  dof = false;
            float dofFocus = 10.0f, dofRange = 8.0f;
        };
        PostSettings post;

        // ---- Weather ----
        // Presets spawn rain / snow particle systems that follow the active camera;
        // storm adds gusts and lightning. `wind` is the global wind vector that
        // particle systems and Water read. Changes fade over `weatherFade` seconds.
        struct WeatherSettings {
            enum Preset { None = 0, Rain = 1, Snow = 2, Storm = 3 };
            int   preset = None;
            float intensity = 0.5f;      // 0..1
            float fade = 2.0f;           // seconds to blend between presets
            Vec3  wind{0, 0, 0};         // world units / second
            std::string thunderSound;    // optional clip played on a lightning strike
        };
        WeatherSettings weather;
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
    /// The cameras that can render now (enabled, active in the hierarchy), lowest
    /// depth first; equal depths keep scene order. Render them in this order: the
    /// first clears the frame, later ones follow Camera::ClearPlan.
    std::vector<Camera*> CameraStack() const;
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
    const std::unordered_map<std::string, std::vector<GameObject*>>& TagIndex() const;
    const std::vector<GameObject*>* TagList(const std::string& tag) const;   // checked against what the objects carry now
    struct BusyScope;                  // marks the stretches where component lists are being walked
    int m_busy = 0;
    bool m_clearPending = false;       // a Clear / RequestClear waiting for the end of the walk
    void ApplyPendingClear();          // runs it once nothing is being walked
    void FlushDestroyedNow();          // FlushDestroyed without the queued Clear
    void DetachDying(const std::vector<GameObject*>& batch, const std::unordered_set<const GameObject*>& inScene);
    mutable std::unordered_map<std::string, std::vector<GameObject*>> m_tagIndex;
    mutable std::uint64_t m_tagIndexStructure = 0, m_tagIndexTags = 0;
    mutable std::size_t   m_tagIndexCount = 0;
    void StepPhysics(float deltaTime);
    void RunFixedUpdate(float step);   // Component::FixedUpdate for every component that can run
    void DestroyAllLive();             // OnDisable + OnDestroy for live components (Clear, ~Scene)
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
    void DispatchActivation();   // OnEnable / OnDisable for components switched on/off
    void StartComponent(Component* c);   // OnEnable (if owed), then Start
    void TickDelayedDestroys(float deltaTime);
    void StepAsyncLoads();
    void DestroyLoadedObjects(const std::vector<std::uint64_t>& ids);
    /// DontDestroyOnLoad objects taken out of the scene while it is replaced.
    struct KeptState {
        std::unordered_set<const GameObject*> keep;
        std::vector<std::unique_ptr<GameObject>> objects;
        std::vector<Component*> active, pending;
        std::vector<GameObject*> marked;
    };
    void DetachKept(KeptState& k);
    void ReattachKept(KeptState& k);
    struct LoadRecord {
        int handle = 0;
        std::unique_ptr<SceneLoadJob> job;
        AsyncLoadOptions opts;
        std::vector<Component*> held;   // made by the job, not yet adopted
        bool finished = false, ok = false;
    };
    LoadRecord* FindLoad(int handle);
    const LoadRecord* FindLoad(int handle) const;
    std::vector<LoadRecord> m_loads;
    int m_nextLoadHandle = 1;

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
    std::unordered_set<const GameObject*> m_destroyQueued;   // members of m_destroyQueue, for O(1) "already queued"
    std::vector<Component*>* m_flushBatch = nullptr;         // the batch FlushPending is walking (removals null its entries)
    std::string m_lastLoadError;
    std::string m_pendingLoad;
    bool        m_hasPendingLoad = false;
    std::function<void(bool)> m_pendingLoadDone;
    std::vector<GameObject*> m_dontDestroy;                       // marked by DontDestroyOnLoad
    std::vector<std::pair<std::uint64_t, float>> m_delayedDestroy;   // instance id, seconds left
    std::string m_pendingText;
    std::function<void(Scene&)> m_pendingTextAfter;
    bool        m_hasPendingText = false;
    std::vector<std::pair<std::string, Vec3>> m_pendingMerges;   // deferred additive scene merges
    std::vector<std::vector<const Component*>*> m_removalWatches;   // live RemovalWatch lists
};

/// True when `go`'s scene lets scripts run (see Scene::runScripts).
inline bool ScriptsRun(const GameObject* go) {
    const Scene* s = go ? go->scene() : nullptr;
    return !s || s->runScripts;
}

} // namespace okay
