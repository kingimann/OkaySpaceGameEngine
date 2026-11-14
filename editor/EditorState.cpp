#include "okay/Net/Matchmaker.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "EditorState.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Components/FirstPersonHands.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/ColliderFit.hpp"
#include "okay/Scene/ClearSpawn.hpp"
#include "okay/Core/FrameOverlay.hpp"
#include "okay/Core/TimerDebug.hpp"
#include "okay/Scene/PrefabTools.hpp"
#include <map>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../shared/ProjectCreate.hpp"
#include "../shared/FileDialog.hpp"      // CancelBuiltin: scene rebuilds drop queued built-in dialogs (#732)

namespace {
// Every new 3D primitive ships with a collider fitted to its mesh, so
// objects block and can be stood on the moment they're created. A box matches most
// primitives; it's auto-fit so it tracks the mesh.
void AddFittedBoxCollider(okay::GameObject* go) {
    if (!go || go->GetComponent<okay::Collider3D>()) return;   // don't double up
    auto* bc = go->AddComponent<okay::BoxCollider3D>();
    bc->autoFit = true;
    okay::FitColliders(go);
}
} // namespace

namespace okay::editor {

EditorState::EditorState() : m_scene("Untitled") {
    m_scene.runScripts = false;   // game code only runs in Play
    m_access = std::make_shared<SceneAccess>();
    m_access->capture = [this] { return SceneSerializer::Serialize(m_scene); };
    m_access->apply = [this](const std::string& text, const SelectionRef& sel) { return ApplySceneText(text, sel); };
    m_stack->SetMaxCommands(kMaxUndo);
    m_stack->SetMergeWindow(1.0f);   // the same field nudged again within a second is one step (see PushUndoText)
    // Online services are part of the engine: a Steam simulation backend by
    // default, the real Steamworks backend when built with -DOKAY_WITH_STEAM.
    m_steam = CreateSteamService();
    SteamConfig sc; sc.appId = 480; // Spacewar test app id
    if (m_steam) m_steam->Initialize(sc);
    // The Play loop: the player's frame clamp (0.1 s), and the scene tick as its update.
    GameLoop::Options lo;
    lo.maxDelta = 0.1f;
    m_loop.SetOptions(lo);
    m_loop.OnUpdate = [this](float dt) {
        const bool editorPaused = paused();
        if (editorPaused && !m_loop.WasStepFrame()) return;   // the Pause button freezes the scene entirely
        // The Step button advances one frame of a running game: hide the editor's own pause from the
        // scene for that frame (controllers skip a paused game), and put it back after.
        struct Unpause {
            bool on;
            explicit Unpause(bool o) : on(o) { if (on) Game::ClearPause(kPauseReason); }
            ~Unpause() { if (on) Game::PushPause(kPauseReason); }
        } unpause(editorPaused);
        if (playTick) playTick(dt); else Tick(dt);
    };
    // "Run N frames": the loop paused the game under the editor's own reason; let the toolbar's Pause flag follow.
    m_loop.OnRunFramesDone = [this] { if (onRunFramesDone) onRunFramesDone(); };
    StartDocument();   // an empty scene is nothing to save
}

bool EditorState::StartHost(std::uint16_t port) {
    StopNetwork();
    GameObject* go = m_scene.CreateGameObject("__Network");
    go->transient = true;   // a preview session's plumbing: never saved into the scene
    m_net = go->AddComponent<NetworkManager>();
    m_net->SetLocalAvatar(go->transform, '@');
    m_net->SetRemoteFactory([this](std::uint32_t id, char) {
        GameObject* peer = m_scene.CreateGameObject("Peer" + std::to_string(id));
        peer->transient = true;
        return peer;
    });
    return m_net->StartServer(port);
}

bool EditorState::StartJoin(const std::string& host, std::uint16_t port) {
    StopNetwork();
    GameObject* go = m_scene.CreateGameObject("__Network");
    go->transient = true;   // a preview session's plumbing: never saved into the scene
    m_net = go->AddComponent<NetworkManager>();
    m_net->SetLocalAvatar(go->transform, '@');
    m_net->SetRemoteFactory([this](std::uint32_t id, char) {
        GameObject* peer = m_scene.CreateGameObject("Peer" + std::to_string(id));
        peer->transient = true;
        return peer;
    });
    return m_net->StartClient(host, port);
}

void EditorState::StopNetwork() {
    if (!m_net) return;
    GameObject* go = m_net->gameObject;
    m_net->Stop();
    if (go) { m_scene.Destroy(go); m_scene.FlushDestroyed(); }
    m_net = nullptr;
}

void EditorState::TickServices(float dt) {
    m_stack->Tick(dt);   // the undo merge window
    if (m_steam) m_steam->RunCallbacks();
    // While playing, the scene's own Update() already ticks m_net (it's a scene
    // component); ticking it again here would double-step the netcode. Only drive
    // it from here in edit mode (so Host/Join previews work without pressing Play).
    if (m_net && !m_playing) m_net->Update(dt);
}

void EditorState::Achievement(const std::string& id) {
    if (m_steam) m_steam->UnlockAchievement(id);
}

// ---- Undo / Redo ---------------------------------------------------------------------------------------

SelectionRef EditorState::CaptureSelection() const {
    SelectionRef r;
    r.valid = true;
    for (GameObject* g : m_multi) if (g) r.guids.push_back(g->Guid());
    if (m_selected) {
        r.primary = m_selected->Guid();
        if (std::find(r.guids.begin(), r.guids.end(), r.primary) == r.guids.end()) r.guids.push_back(r.primary);   // Create* select without adding to the set
    }
    return r;
}

// Replace the scene with `text` and select, by GUID, what `sel` names (an invalid `sel` keeps the current
// selection). Every object is rebuilt, so the pointers change and the GUIDs carry the selection across.
bool EditorState::ApplySceneText(const std::string& text, const SelectionRef& sel) {
    if (finishFrozenTick) finishFrozenTick();   // never rebuild a scene frozen mid-frame
    okay::filedialog::CancelBuiltin();   // every object is rebuilt; a queued dialog may hold one of its pointers (#732)
    const SelectionRef want = sel.valid ? sel : CaptureSelection();
    StopNetwork();
    if (!SceneSerializer::Deserialize(m_scene, text)) return false;   // all or nothing: the scene is as it was
    Select(nullptr);   // Deserialize freed every object; clear m_multi too (no dangling ptrs)
    GameObject* prim = nullptr;
    for (const auto& up : m_scene.Objects()) {
        if (up->transient) continue;
        const std::string& g = up->Guid();
        if (std::find(want.guids.begin(), want.guids.end(), g) == want.guids.end()) continue;
        if (m_multi.empty()) Select(up.get()); else ToggleSelect(up.get());
        if (g == want.primary) prim = up.get();
    }
    if (prim) m_selected = prim;
    m_known = SceneSerializer::Serialize(m_scene);
    dirty = Fingerprint(m_known) != m_savedPrint;   // back on the saved text means nothing is unsaved
    return true;
}

void EditorState::RestoreKeepingSelection(const std::string& snapshot) {
    ApplySceneText(snapshot, SelectionRef{});
    dirty = true;
}

void EditorState::RecordStep(const std::string& before, const std::string& after, const std::string& name,
                             const SelectionRef& selBefore, std::uint64_t key) {
    if (!m_access) return;
    const std::string label = name.empty() ? DescribeSceneChange(before, after) : name;
    SelectionRef selAfter = selBefore.valid ? CaptureSelection() : SelectionRef{};
    m_stack->Record(std::make_unique<SceneDeltaCommand>(label, m_access, TextDelta::Make(before, after),
                                                        selBefore, std::move(selAfter), key));
}

// The boundary: whatever changed since the last one becomes one step (named by the open PushUndo, or by what
// changed). Returns true when a step was recorded.
bool EditorState::SyncHistory() {
    Pending p = std::move(m_pending);
    m_pending = Pending{};
    if (m_playing) return false;   // the running game is not part of the edit history
    std::string cur = SceneSerializer::Serialize(m_scene);
    const bool changed = cur != m_known;
    if (!changed && p.extras.empty()) return false;
    const SelectionRef sel = p.open ? p.sel : SelectionRef{};
    if (p.extras.empty()) {
        RecordStep(m_known, cur, p.open ? p.name : std::string(), sel, 0);
    } else {   // the scene change and the editor-side edits are one step
        std::string label = p.name;
        if (label.empty()) label = changed ? DescribeSceneChange(m_known, cur) : p.extras.front()->Name();
        m_stack->BeginGroup(label);
        for (auto& e : p.extras) m_stack->Record(std::move(e));
        if (changed) RecordStep(m_known, cur, label, sel, 0);
        m_stack->EndGroup();
    }
    if (changed) m_known = std::move(cur);
    return true;
}

void EditorState::CommitUndo() {
    if (m_pending.open) SyncHistory();
}

void EditorState::PushUndo(const std::string& name) {
    if (m_suppressUndo || m_playing) return;
    ++m_undoSerial;
    SyncHistory();   // the previous step ends here (an edit nobody announced becomes its own step)
    m_pending.open = true;
    m_pending.name = name;
    m_pending.sel = CaptureSelection();
}

void EditorState::PushUndo(const std::string& name, std::unique_ptr<ICommand> alsoUndo) {
    if (m_suppressUndo || m_playing) return;
    PushUndo(name);
    if (alsoUndo) m_pending.extras.push_back(std::move(alsoUndo));
}

namespace {
// Files an edit wrote, as one ICommand of the undo step (see EditorState::PushUndo(name, alsoUndo)).
class FileEditCommand : public ICommand {
public:
    FileEditCommand(std::vector<EditorState::FileEdit> e, std::string name) : m_edits(std::move(e)), m_name(std::move(name)) {}
    bool Do() override { Apply(false); return true; }
    bool Undo() override { Apply(true); return true; }
    std::string Name() const override { return m_name; }
    std::size_t MemoryBytes() const override {
        std::size_t n = 64;
        for (const auto& e : m_edits) n += e.path.size() + e.before.size() + e.after.size();
        return n;
    }
private:
    void Apply(bool undoing) {
        namespace fs = std::filesystem;
        for (const EditorState::FileEdit& e : m_edits) {
            std::error_code ec;
            const bool keep = undoing ? e.hadBefore : e.hasAfter;
            if (!keep) { fs::remove(e.path, ec); continue; }
            const std::string tmp = e.path + ".tmp";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                f << (undoing ? e.before : e.after);
            }
            fs::rename(tmp, e.path, ec);
            if (ec) { fs::remove(e.path, ec); ec.clear(); fs::rename(tmp, e.path, ec); }
        }
        if (!m_edits.empty()) SceneSerializer::ClearPrefabCache();   // instantiating a prefab must read the restored file
    }
    std::vector<EditorState::FileEdit> m_edits;
    std::string m_name;
};
}

std::unique_ptr<ICommand> EditorState::MakeFileUndo(std::vector<FileEdit> edits, const std::string& name) {
    return std::make_unique<FileEditCommand>(std::move(edits), name);
}

bool EditorState::ReadTextFile(const std::string& path, std::string& out) {
    out.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool EditorState::AttachUndoFiles(std::vector<FileEdit> edits) {
    if (m_suppressUndo || m_playing || edits.empty()) return false;
    if (m_pending.open) {
        m_pending.extras.push_back(MakeFileUndo(std::move(edits), m_pending.name.empty() ? "Edit Prefab" : m_pending.name));
        return true;
    }
    PushUndo("Edit Prefab", MakeFileUndo(std::move(edits)));
    return true;
}

void EditorState::DiscardLastUndo() {
    if (m_pending.open && m_pending.extras.empty()) m_pending = Pending{};
}

void EditorState::PushUndoText(std::string before, const std::string& name, std::uint64_t mergeKey) {
    if (m_suppressUndo || m_playing) return;
    ++m_undoSerial;
    if (!m_pending.extras.empty()) { SyncHistory(); return; }   // editor-side edits are waiting: they and this change share one step
    Pending p = std::move(m_pending);
    m_pending = Pending{};
    if (m_known != before) {   // something changed before the edit being recorded: a step of its own
        RecordStep(m_known, before, p.open ? p.name : std::string(), p.open ? p.sel : SelectionRef{}, 0);
        m_known = before;
    }
    std::string cur = SceneSerializer::Serialize(m_scene);
    if (cur == m_known) return;
    RecordStep(m_known, cur, name, SelectionRef{}, mergeKey);
    m_known = std::move(cur);
}

bool EditorState::Undo() {
    if (m_playing) return false;
    // The step's objects go away: a built-in dialog queued earlier would write through a
    // pointer into them on accept (#732). The same for Redo and Load.
    okay::filedialog::CancelBuiltin();
    ++m_undoSerial;
    SyncHistory();
    if (!m_stack->CanUndo()) return false;
    return m_stack->Undo();
}

bool EditorState::Redo() {
    if (m_playing) return false;
    okay::filedialog::CancelBuiltin();   // a queued dialog may hold a pointer into the rebuilt scene (#732)
    ++m_undoSerial;
    SyncHistory();
    if (!m_stack->CanRedo()) return false;
    return m_stack->Redo();
}

std::vector<std::string> EditorState::UndoNames(std::size_t max) const {
    std::vector<std::string> v;
    if (m_pending.open && !m_playing) v.push_back(m_pending.name.empty() ? std::string("Edit") : m_pending.name);
    for (std::string& n : m_stack->UndoNames(max)) { if (max && v.size() >= max) break; v.push_back(std::move(n)); }
    return v;
}

std::vector<std::string> EditorState::RedoNames(std::size_t max) const {
    return CanRedo() ? m_stack->RedoNames(max) : std::vector<std::string>{};
}

// A new, opened or created scene is a new document: its own empty history, this text is the baseline.
void EditorState::StartDocument() {
    if (!m_levels.empty()) { m_levels.clear(); m_ses = PrefabSession{}; }   // a new document replaces a prefab session (the editor asks first)
    m_stack->Clear();
    m_pending = Pending{};
    m_known = SceneSerializer::Serialize(m_scene);
    m_savedPrint = Fingerprint(m_known);
    m_stack->MarkSaved();
    dirty = false;
}

GameObject* EditorState::CreateEmpty(const std::string& name) {
    PushUndo();
    GameObject* go = m_scene.CreateGameObject(name);
    m_selected = go;
    dirty = true;
    return go;
}

GameObject* EditorState::CreateSprite(const std::string& name) {
    PushUndo();
    GameObject* go = m_scene.CreateGameObject(name);
    auto* sr = go->AddComponent<SpriteRenderer>();
    sr->glyph = '#';
    sr->color = Color::White;
    m_selected = go;
    dirty = true;
    return go;
}

GameObject* EditorState::CreateCamera(const std::string& name) {
    PushUndo();
    GameObject* go = m_scene.CreateGameObject(name);
    auto* cam = go->AddComponent<Camera>();
    if (view3D) {
        // Start where the Scene view is looking from, so the new camera shows
        // what you were just looking at (instead of sitting inside the ground
        // at the origin). Cameras look down their local -Z.
        const float yr = camYaw * Mathf::Deg2Rad, pr = camPitch * Mathf::Deg2Rad;
        const Vec3 dir{Mathf::Cos(pr) * Mathf::Sin(yr), Mathf::Sin(pr), Mathf::Cos(pr) * Mathf::Cos(yr)};
        go->transform->localPosition = camTarget + dir * camDist;
        go->transform->localRotation = Quat::LookRotation(dir);
        cam->projection = Camera::Projection::Perspective;
    } else {
        go->transform->localPosition = Vec3{camTarget.x, camTarget.y, 10.0f};
        cam->projection = Camera::Projection::Orthographic;
    }
    AdoptCamera(cam);
    m_selected = go;
    dirty = true;
    return go;
}

void EditorState::AdoptCamera(Camera* cam) {
    if (!cam) return;
    // Drop the placeholder "Main Camera" if it's still exactly as the editor made it.
    if (autoCamera && autoCamera != cam->gameObject) {
        bool alive = false;
        for (const auto& up : m_scene.Objects()) if (up.get() == autoCamera) { alive = true; break; }
        // Untouched = only Transform + Camera, no children or parent, and still at
        // the spot EnsureSceneCamera put it ((0,2,10) in 3D, (0,0,10) in 2D).
        auto untouched = [&]() {
            Transform* t = autoCamera->transform;
            if (!t || autoCamera->Components().size() > 2 || !t->Children().empty() || t->Parent()) return false;
            const Vec3 p = t->localPosition;
            return (p - Vec3{0, 2, 10}).Magnitude() < 1e-3f || (p - Vec3{0, 0, 10}).Magnitude() < 1e-3f;
        };
        if (alive && untouched()) {
            if (m_selected == autoCamera) m_selected = nullptr;
            m_scene.Destroy(autoCamera);
            m_scene.FlushDestroyed();
        }
        autoCamera = nullptr;
    }
    for (const auto& up : m_scene.Objects())
        if (up)
            if (auto* c = up->GetComponent<Camera>()) c->main = (c == cam);
    m_scene.mainCamera = cam;
    dirty = true;
}

GameObject* EditorState::SettleSpawn(GameObject* go) {
    if (!go || !go->transform) return go;
    if (spawnPlaceHook && spawnPlaceHook(go)) return go;   // "Place on surface" put it under the cursor
    const Vec3 hint = (camTarget.x != 0.0f || camTarget.z != 0.0f) ? camTarget : Vec3::Zero;
    go->transform->SetPosition(ClearFeetPosition(m_scene, go, hint));
    return go;
}

GameObject* EditorState::CreateCube(const std::string& name) {
    PushUndo();
    GameObject* go = m_scene.CreateGameObject(name);
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube();
    mr->color = Color::FromBytes(200, 200, 205); // neutral gray
    AddFittedBoxCollider(go);
    SettleSpawn(go);
    m_selected = go;
    view3D = true;
    dirty = true;
    return go;
}

GameObject* EditorState::CreatePyramid(const std::string& name) {
    PushUndo();
    GameObject* go = m_scene.CreateGameObject(name);
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Pyramid();
    mr->color = Color::FromBytes(200, 200, 205); // neutral gray
    AddFittedBoxCollider(go);
    SettleSpawn(go);
    m_selected = go;
    view3D = true;
    dirty = true;
    return go;
}

GameObject* EditorState::CreateMesh(const std::string& meshName) {
    PushUndo();
    GameObject* go = m_scene.CreateGameObject(meshName);
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::FromName(meshName);
    mr->color = Color::FromBytes(200, 200, 205); // neutral gray
    AddFittedBoxCollider(go);
    SettleSpawn(go);
    m_selected = go;
    view3D = true;
    dirty = true;
    return go;
}

// True if any ANCESTOR of `go` is also in `set` (so we only act on top-level picks).
static bool AncestorInSet(GameObject* go, const std::vector<GameObject*>& set) {
    if (!go || !go->transform) return false;
    for (Transform* t = go->transform->Parent(); t; t = t->Parent())
        if (t->gameObject && std::find(set.begin(), set.end(), t->gameObject) != set.end()) return true;
    return false;
}

GameObject* EditorState::DuplicateSelected() {
    if (m_multi.empty() && !m_selected) return nullptr;
    PushUndo();
    std::vector<GameObject*> targets = m_multi.empty() ? std::vector<GameObject*>{m_selected} : m_multi;
    std::vector<GameObject*> clones;
    for (GameObject* g : targets) {
        if (!g || AncestorInSet(g, targets)) continue;   // a parent clone already brings its children
        // Duplicate keeps the original's parent and local pose (a child stays a child), puts the copy
        // right after it among its siblings and gives it a free name ("Cube (1)"). Nudged in WORLD
        // space, so a child of a scaled parent still lands a visible half unit away.
        GameObject* clone = m_scene.Duplicate(*g);
        if (clone) { clone->transform->SetPosition(clone->transform->Position() + Vec3{0.5f, 0.5f, 0.0f}); clones.push_back(clone); }
    }
    if (clones.empty()) return nullptr;
    if (duplicatePlaceHook) duplicatePlaceHook(clones);   // "Place on surface" moves the copies onto the surface under the cursor
    m_multi = clones;
    m_selected = clones.back();
    dirty = true;
    return m_selected;
}

GameObject* EditorState::GroupSelected(const std::string& name) {
    std::vector<GameObject*> targets = m_multi.empty()
        ? (m_selected ? std::vector<GameObject*>{m_selected} : std::vector<GameObject*>{})
        : m_multi;
    if (targets.empty()) return nullptr;
    PushUndo("Group Selection");
    // The group sits under the first pick's parent; children keep their world place.
    Transform* parent = targets[0]->transform ? targets[0]->transform->Parent() : nullptr;
    GameObject* group = m_scene.CreateGameObject(name);
    if (parent) group->transform->SetParent(parent, false);
    for (GameObject* g : targets) {
        if (!g || g == group || AncestorInSet(g, targets)) continue;
        g->transform->SetParent(group->transform, /*worldPositionStays=*/true);
    }
    Select(group);
    dirty = true;
    return group;
}

void EditorState::DeleteSelected() {
    if (finishFrozenTick) finishFrozenTick();   // never rebuild a scene frozen mid-frame
    if (m_multi.empty() && !m_selected) return;
    okay::filedialog::CancelBuiltin();   // a queued dialog may hold a pointer to a deleted object (#732)
    PushUndo();
    // Delete the whole selection (multi-select), not just the primary object.
    std::vector<GameObject*> targets = m_multi.empty() ? std::vector<GameObject*>{m_selected} : m_multi;
    for (GameObject* g : targets) if (g) m_scene.Destroy(g);
    // Flush the destroy queue WITHOUT simulating a frame. Update(0) would run every
    // component's Update/LateUpdate + both physics steps in EDIT mode — ticking
    // half-torn-down objects (and a controller/camera-follow that references the
    // object being deleted) is a needless hazard. FlushDestroyed only reaps the queue.
    m_scene.FlushDestroyed();
    m_selected = nullptr;
    m_multi.clear();
    dirty = true;
}

int EditorState::DeleteChildrenOfSelected() {
    if (finishFrozenTick) finishFrozenTick();   // never rebuild a scene frozen mid-frame
    const std::vector<GameObject*> targets = m_multi.empty() ? std::vector<GameObject*>(m_selected ? 1 : 0, m_selected) : m_multi;
    bool any = false;
    for (const GameObject* g : targets) if (g && g->transform && g->transform->ChildCount() > 0) any = true;
    if (!any) return 0;
    PushUndo("Delete Children");
    int removed = 0;
    for (GameObject* g : targets) {
        if (!g || g->IsDestroyed() || !g->transform) continue;
        // Anything below `g` leaves the selection before it is freed, so no selected pointer dangles.
        for (const auto& up : m_scene.Objects())
            if (up.get() != g && up->IsSelfOrDescendantOf(g)) Deselect(up.get());
        removed += m_scene.DestroyChildren(g);
    }
    m_scene.FlushDestroyed();   // no simulated frame in edit mode (see DeleteSelected)
    dirty = true;
    return removed;
}

void EditorState::NewScene() {
    if (finishFrozenTick) finishFrozenTick();   // never rebuild a scene frozen mid-frame
    if (m_playing) Stop();
    StopNetwork();
    okay::filedialog::CancelBuiltin();   // a queued dialog may hold a pointer into the cleared scene (#732)
    m_scene.Clear();
    m_scene.SetName("Untitled");
    // Every scene starts with a Main Camera so a fresh/empty scene can
    // render and Play right away. Templates Clear() the scene and add their own, so
    // this never doubles up.
    GameObject* camObj = m_scene.CreateGameObject("Main Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->localPosition = {0, 2, 10};
    m_scene.mainCamera = cam;
    Select(nullptr);   // Clear() freed every object; clear m_multi too (no dangling ptrs)
    m_path.clear();
    StartDocument();
}

void EditorState::NewScene2D() {
    NewScene();
    m_suppressUndo = true; // batch the template objects into one undo step
    m_scene.SetName("Untitled 2D");
    if (Camera* c = m_scene.mainCamera) {   // a 2D camera looks straight at the origin
        c->projection = Camera::Projection::Orthographic;
        if (c->transform) c->transform->localPosition = {0, 0, 10};
    }
    GameObject* sp = CreateSprite("Sprite");
    sp->GetComponent<SpriteRenderer>()->color = Color::Green;
    m_suppressUndo = false;
    view3D = false;
    m_selected = sp;
    StartDocument();
}

void EditorState::NewScene3D() {
    NewScene();
    m_suppressUndo = true;
    m_scene.SetName("Untitled 3D");
    // NewScene already made a perspective Main Camera at {0,2,10}; reuse it.

    // A polished, populated default (like default level, not an empty
    // void): a ground plane, a warm sun with soft sky fill, and a couple of hero
    // shapes — so lighting, shadows, sky and reflections all read immediately.

    // ---- Ground plane: large, receives shadows, matte neutral ----
    GameObject* ground = m_scene.CreateGameObject("Ground");
    {
        auto* mr = ground->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Plane(60.0f);
        mr->color = Color::FromBytes(150, 154, 150);   // soft neutral, not pure gray
        mr->specular = 0.05f; mr->shininess = 16.0f;
        mr->groundShadow = false;                       // it IS the ground
        auto* groundCol = ground->AddComponent<MeshCollider3D>();
        groundCol->autoFit = true;
        FitColliders(ground);                         // the plane is the floor, not a 1 m box
    }

    // ---- Hero shapes on the ground ----
    GameObject* cube = m_scene.CreateGameObject("Cube");
    {
        auto* mr = cube->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::RoundedBox();
        mr->color = Color::FromBytes(196, 128, 96);     // warm terracotta
        mr->specular = 0.3f; mr->shininess = 40.0f;
        cube->transform->localPosition = {-1.1f, 0.5f, 0.0f};
        AddFittedBoxCollider(cube);
    }
    GameObject* ball = m_scene.CreateGameObject("Sphere");
    {
        auto* mr = ball->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Sphere(0.5f, 24, 32);
        mr->color = Color::FromBytes(150, 165, 190);    // cool bluish, slightly glossy/metal
        mr->specular = 0.7f; mr->shininess = 90.0f;
        mr->metallic = 0.35f; mr->reflectivity = 0.25f;
        ball->transform->localPosition = {0.9f, 0.55f, 0.4f};
        ball->transform->localScale = {1.1f, 1.1f, 1.1f};
        AddFittedBoxCollider(ball);
    }

    // ---- Sun: warm directional key light at a cinematic angle ----
    GameObject* light = m_scene.CreateGameObject("Sun");
    {
        auto* L = light->AddComponent<Light>();
        L->type = Light::Type::Directional;
        L->useTemperature = true; L->temperature = 5600.0f;   // warm daylight
        L->color = Light::KelvinToColor(5600.0f);
        L->intensity = 1.15f;
        L->ambient = 0.30f;
        L->ambientColor = Color::FromBytes(150, 175, 210);    // sky-blue fill in shadow
        light->transform->localRotation = Quat::Euler({48, -35, 0});
    }

    // ---- Scene lighting/atmosphere: rich sky, aligned sun disc, gentle fog ----
    auto& rs = m_scene.renderSettings;
    rs.skybox     = true;
    rs.skyTop     = Color::FromBytes(74, 128, 208);     // deeper zenith blue
    rs.skyHorizon = Color::FromBytes(196, 214, 232);    // pale haze at the horizon
    rs.skyBottom  = Color::FromBytes(150, 150, 152);    // ground-ish under the horizon
    rs.skyHorizonPos = 0.52f;
    rs.ambient    = 0.22f;
    rs.skySun     = true;                               // a soft sun disc + glow in the sky
    rs.skySunX = 0.34f; rs.skySunY = 0.24f; rs.skySunSize = 0.045f;
    rs.skySunColor = Color::FromBytes(255, 244, 214);
    rs.fog = true;                                      // subtle depth haze (hides the far edge)
    rs.fogColor = Color::FromBytes(200, 216, 230);
    rs.fogStart = 35.0f; rs.fogEnd = 110.0f;
    rs.tonemap = true;                                 // filmic (already the default)
    rs.vignette = 0.12f;                               // gentle focus

    // Shadows + sky reflections look best on; leave them at their global on-defaults.

    // Frame the editor orbit camera on the shapes.
    camTarget = {0, 0.5f, 0.0f};
    camDist = 7.0f;

    m_suppressUndo = false;
    view3D = true;
    m_selected = cube;
    StartDocument();
}

void EditorState::NewPlatformer() {
    NewScene();
    m_suppressUndo = true;
    Templates::Platformer(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewPlatformer3D() {
    NewScene();
    m_suppressUndo = true;
    Templates::Platformer3D(m_scene);
    m_suppressUndo = false;
    view3D = true;
    camTarget = {0, 1, 0};
    camDist = 14.0f;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewFPS() {
    NewScene();
    m_suppressUndo = true;
    Templates::FPS(m_scene);
    m_suppressUndo = false;
    view3D = true;
    camTarget = {0, 1, 0};
    camDist = 10.0f;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewTerrainSandbox() {
    NewScene();
    m_suppressUndo = true;
    Templates::TerrainSandbox(m_scene);
    m_suppressUndo = false;
    view3D = true;
    camTarget = {0, 2, 0};
    camDist = 30.0f;
    m_selected = m_scene.Find("Terrain");
    StartDocument();
}

void EditorState::NewVoxelSandbox() {
    NewScene();
    m_suppressUndo = true;
    Templates::VoxelSandbox(m_scene);
    m_suppressUndo = false;
    view3D = true;
    camTarget = {0, 4, 0};
    camDist = 36.0f;
    m_selected = m_scene.Find("Voxel Terrain");
    StartDocument();
}

void EditorState::NewThirdPerson() {
    NewScene();
    m_suppressUndo = true;
    Templates::ThirdPerson(m_scene);
    m_suppressUndo = false;
    view3D = true;
    camTarget = {0, 1, 0};
    camDist = 8.0f;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewHumanoid() {
    NewScene();
    m_suppressUndo = true;
    Templates::Humanoid(m_scene);
    m_suppressUndo = false;
    view3D = true;
    camTarget = {0, 1, 0};
    camDist = 8.0f;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewThirdPersonShooter() {
    NewScene();
    m_suppressUndo = true;
    Templates::ThirdPersonShooter(m_scene);
    m_suppressUndo = false;
    view3D = true;
    camTarget = {0, 1, 0};
    camDist = 8.0f;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewPointAndClick() {
    NewScene();
    m_suppressUndo = true;
    Templates::PointAndClick(m_scene);
    m_suppressUndo = false;
    view3D = true;
    camTarget = {0, 1, 0};
    camDist = 14.0f;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewMultiplayer() {
    NewScene();
    m_suppressUndo = true;
    Templates::Multiplayer(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewTopDown() {
    NewScene();
    m_suppressUndo = true;
    Templates::TopDown(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewTopDownShooter() {
    NewScene();
    m_suppressUndo = true;
    Templates::TopDownShooter(m_scene);
    m_suppressUndo = false;
    view3D = true;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewPlatformer25D() {
    NewScene();
    m_suppressUndo = true;
    Templates::Platformer25D(m_scene);
    m_suppressUndo = false;
    view3D = true;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewVehicle3D() {
    NewScene();
    m_suppressUndo = true;
    Templates::Vehicle3D(m_scene);
    m_suppressUndo = false;
    view3D = true;
    camTarget = {0, 1, 0};
    camDist = 12.0f;
    m_selected = m_scene.Find("Car");
    StartDocument();
}

void EditorState::NewVehicle2D() {
    NewScene();
    m_suppressUndo = true;
    Templates::Vehicle2D(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Car");
    StartDocument();
}

void EditorState::NewCoinCollector() {
    NewScene();
    m_suppressUndo = true;
    Templates::CoinCollector(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Player");
    StartDocument();
}

void EditorState::NewMainMenu() {
    NewScene();
    m_suppressUndo = true;
    Templates::MainMenu(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("StartButton");
    StartDocument();
}

void EditorState::NewSnake() {
    NewScene();
    m_suppressUndo = true;
    Templates::Snake(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Board");
    StartDocument();
}

void EditorState::NewBrickBreaker() {
    NewScene();
    m_suppressUndo = true;
    Templates::BrickBreaker(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Paddle");
    StartDocument();
}

void EditorState::NewInventory() {
    NewScene();
    m_suppressUndo = true;
    Templates::Inventory(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Bag");
    StartDocument();
}

int EditorState::SaveEditedMeshAssets(std::string* report) {
    // A mesh edited since it was loaded from its .okaymesh file is written back
    // to that file, and every other object using the file picks the change up.
    int written = 0;
    for (const auto& up : m_scene.Objects()) {
        auto* mr = up ? up->GetComponent<MeshRenderer>() : nullptr;
        if (!mr || !modeling::IsOkayMeshPath(mr->meshPath) || mr->mesh.vertices.empty()) continue;
        const std::uint64_t h = modeling::GeometryHash(mr->mesh);
        if (mr->meshPathHash != 0 && h == mr->meshPathHash) continue;
        if (mr->meshPathHash == 0) {   // not loaded through the serializer: compare with the file
            Mesh onDisk;
            if (modeling::LoadMeshAsset(mr->meshPath, onDisk) && modeling::GeometryHash(onDisk) == h) { mr->meshPathHash = h; continue; }
        }
        std::string err;
        if (!modeling::SaveMeshAsset(mr->meshPath, mr->mesh, &err)) {
            if (report) *report += "couldn't write " + mr->meshPath + ": " + err + "\n";
            continue;
        }
        ++written;
        if (report) *report += "wrote " + mr->meshPath + "\n";
        // What the file now holds (quads re-triangulated), for this object and the others using it.
        Mesh fresh;
        modeling::LoadMeshAsset(mr->meshPath, fresh);
        const std::uint64_t fh = modeling::GeometryHash(fresh);
        mr->meshPathHash = fh;
        const std::string path = mr->meshPath;
        for (const auto& other : m_scene.Objects()) {
            auto* omr = other && other.get() != up.get() ? other->GetComponent<MeshRenderer>() : nullptr;
            if (omr && omr->meshPath == path) { omr->mesh = fresh; omr->meshPathHash = fh; }
        }
        // This object takes what the file holds too, so the scene doesn't also keep
        // a private copy. Vertex numbers are kept; triangles may be re-ordered.
        mr->mesh = fresh;
        if (onMeshTrianglesReordered) onMeshTrianglesReordered(up.get());
    }
    return written;
}

std::uint64_t EditorState::Fingerprint(const std::string& text) {
    std::uint64_t h = 1469598103934665603ull;   // FNV-1a
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ull; }
    return h ^ ((std::uint64_t)text.size() << 1);
}

bool EditorState::RewriteAssetPaths(const std::function<std::string(const std::string&)>& rewrite) {
    if (m_playing || !rewrite) return false;
    // Prefab mode: the scene and prefabs put aside hold asset paths too. Their histories end here for the same
    // reason the open one does; a level without unsaved changes still has none afterwards.
    for (Level& lv : m_levels) {
        const std::string t = rewrite(lv.text);
        if (t != lv.text) {
            lv.text = t; lv.known = t;
            lv.stack->Clear();
            if (lv.unsaved) lv.stack->MarkDirty(); else { lv.savedPrint = Fingerprint(t); lv.stack->MarkSaved(); }
        }
        if (!lv.ses.path.empty()) lv.ses.path = rewrite(lv.ses.path);
    }
    if (!m_ses.path.empty()) m_ses.path = rewrite(m_ses.path);
    const bool wasClean = !HasUnsavedChanges();
    SyncHistory();
    const std::string now = SceneSerializer::Serialize(m_scene);
    const std::string next = rewrite(now);
    if (next == now) return false;   // this scene does not use what moved: its history stays valid
    // The history holds pieces of the old scene text, with the old asset paths in them, and cannot be
    // rewritten piece by piece. Undoing past this point would bring the old paths back, so it ends here.
    RestoreKeepingSelection(next);
    m_stack->Clear();
    m_pending = Pending{};
    m_known = SceneSerializer::Serialize(m_scene);
    if (wasClean) MarkClean(); else { m_stack->MarkDirty(); dirty = true; }
    return true;
}

void EditorState::MarkClean() {
    SyncHistory();   // the save point is the scene as it is now, edits nobody announced included
    m_savedPrint = Fingerprint(m_known.empty() || m_playing ? SceneSerializer::Serialize(m_scene) : m_known);
    m_stack->MarkSaved();
    dirty = false;
}

bool EditorState::HasUnsavedChanges() {
    if (m_playing) return m_dirtyAtPlay || m_playPrint != m_savedPrint;   // the scene being edited, not the running copy
    return dirty || Fingerprint(SceneSerializer::Serialize(m_scene)) != m_savedPrint;
}

bool EditorState::SyncDirty() {
    if (m_playing) return m_dirtyAtPlay || m_playPrint != m_savedPrint;
    if (dirty) return true;
    if (Fingerprint(SceneSerializer::Serialize(m_scene)) != m_savedPrint) dirty = true;
    return dirty;
}

bool EditorState::Save(const std::string& path, std::string* error) {
    if (m_playing) {
        if (error) *error = "Stop Play first: saving now would save the running game, not your scene.";
        return false;
    }
    if (!m_levels.empty()) {
        if (error) *error = "A prefab is open: use Save Prefab, or leave prefab mode first (the scene is saved from there).";
        return false;
    }
    SaveEditedMeshAssets();
    if (!SceneSerializer::SaveToFile(m_scene, path, error)) return false;
    m_path = path;
    MarkClean();
    return true;
}

bool EditorState::LoadRecoveredText(const std::string& text, const std::string& keepPath, std::string* error) {
    const std::string tmp = keepPath.empty() ? std::string("okay_scene_recovery.okayscene") : keepPath + ".recovery-tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) { if (error) *error = "couldn't write the recovery file"; return false; }
        f << text;
    }
    if (!Load(tmp, error)) return false;
    m_path = keepPath;
    dirty = true;
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return true;
}

bool EditorState::Load(const std::string& path, std::string* error) {
    if (beforeLoad) {   // the project version gate: nothing is read, let alone changed, for a project nobody has answered for
        std::string why;
        if (!beforeLoad(path, &why)) { if (error) *error = why.empty() ? "This project can't be opened yet." : why; return false; }
    }
    if (!m_levels.empty()) {
        if (error) *error = "A prefab is open. Leave prefab mode (Back) before opening another scene.";
        return false;
    }
    if (finishFrozenTick) finishFrozenTick();   // never rebuild a scene frozen mid-frame
    if (m_playing) Stop();
    StopNetwork();
    okay::filedialog::CancelBuiltin();   // a queued dialog may hold a pointer into the replaced scene (#732)
    if (!SceneSerializer::LoadFromFile(m_scene, path, error)) return false;   // all or nothing: the scene is as it was
    m_path = path;
    // A different scene is a different document: its undo history is its own. (Keeping the old one
    // would let Undo put the previous scene's objects into this file.)
    // Re-home the project to the scene's own project folder (the parent of its
    // Assets/ dir), so opening another project's scene switches the Project panel
    // and asset paths over instead of keeping the previous project's files around.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        for (fs::path p = fs::absolute(fs::path(path), ec).parent_path(); !p.empty(); p = p.parent_path()) {
            if (p.filename() == "Assets") { setProjectDir(p.parent_path().string()); break; }
            if (p == p.parent_path()) break;   // reached the filesystem root
        }
    }
    Select(nullptr);   // load rebuilt the scene; clear m_multi too (no dangling ptrs)
    PoseCapture::SetAssetBaseDir(m_projectDir.empty() ? std::string() : m_projectDir + "/Assets");
    for (const auto& up : m_scene.Objects())
        if (auto* ma = up->GetComponent<ModelAnimator>())
            PoseCapture::ImportFolder(*ma, PoseCapture::AssetBaseDir());
    StartDocument();   // a loaded scene starts a fresh undo history, so Undo can never put the old scene's objects into the new file
    if (onSceneLoaded) onSceneLoaded();
    return true;
}

EditorState::NewProjectResult EditorState::CreateProject(const std::string& root, const std::string& sceneName, void (EditorState::*tpl)(),
                                                         const std::function<bool(const std::string&, std::string&)>& writeFiles) {
    namespace fs = std::filesystem;
    NewProjectResult r;
    if (m_playing) { r.error = "Stop Play first."; return r; }
    if (!m_levels.empty()) { r.error = "A prefab is open. Leave prefab mode (Back) first."; return r; }
    if (finishFrozenTick) finishFrozenTick();
    okayproj::Made made;
    std::string err;
    // 1. Folders and files first, while the open scene is still untouched: a failure here costs nothing.
    if (!okayproj::MakeSkeleton(fs::path(root), made, err)) { r.error = err; return r; }
    if (writeFiles && !writeFiles(root, err)) {
        okayproj::Undo(made);
        r.error = err;
        return r;
    }
    // 2. Put the previous state aside, build the template, and save its first scene.
    const std::string prevText = SceneSerializer::Serialize(m_scene);
    const std::string prevPath = m_path, prevDir = m_projectDir, prevName = m_scene.Name();
    const bool prevDirty = dirty;
    const std::uint64_t prevPrint = m_savedPrint;
    std::unique_ptr<UndoStack> prevStack = std::move(m_stack);   // the open scene's history, kept in case this fails
    const std::string prevKnown = m_known;
    Pending prevPending = std::move(m_pending);
    m_stack = std::make_unique<UndoStack>();
    m_stack->SetMaxCommands(kMaxUndo);
    m_stack->SetMergeWindow(1.0f);
    m_pending = Pending{};
    const std::string scenePath = (fs::path(root) / "Assets" / (sceneName + ".okayscene")).string();
    (this->*tpl)();
    m_scene.SetName(sceneName);
    setProjectDir(root);
    if (!Save(scenePath, &err)) {
        // Put everything back exactly as it was.
        StopNetwork();
        SceneSerializer::Deserialize(m_scene, prevText);
        m_scene.SetName(prevName);
        Select(nullptr);
        m_path = prevPath;
        setProjectDir(prevDir);
        m_stack = std::move(prevStack);
        m_known = prevKnown;
        m_pending = std::move(prevPending);
        dirty = prevDirty;
        m_savedPrint = prevPrint;
        okayproj::Undo(made);   // Save is atomic: a failed one wrote nothing, so only the folders and files made above go
        r.error = err;
        return r;
    }
    m_stack->Clear();   // a new project starts with a fresh history
    m_pending = Pending{};
    m_known = SceneSerializer::Serialize(m_scene);
    m_stack->MarkSaved();
    r.ok = true;
    r.root = root;
    r.scene = scenePath;
    return r;
}

void EditorState::Play() {
    if (m_playing || !m_levels.empty()) return;   // a prefab is open: there is no game to run (PlayBlockedReason says why)
    SyncHistory();   // close the open undo step: what Play changes is thrown away and never recorded
    // SyncHistory() above left m_known equal to the scene's text right now (it serializes the scene to find out what
    // changed), so the snapshot is that text: serializing a second time cost as much again on a big scene.
    m_snapshot = m_known;                              // remember edit state
    m_playPrint = Fingerprint(m_snapshot);            // what "unsaved changes" means while the game runs
    m_dirtyAtPlay = dirty;
    m_scene.UseFixedStepPhysics(true);   // physics as the game runs it: fixed steps, drawn smoothly
    Character::SetAssetBaseDir(m_projectDir);   // a Character's project-relative clips file
    PoseCapture::SetAssetBaseDir(m_projectDir.empty() ? std::string() : m_projectDir + "/Assets");
    for (const auto& up : m_scene.Objects())
        if (auto* ma = up->GetComponent<ModelAnimator>())
            PoseCapture::ImportFolder(*ma, PoseCapture::AssetBaseDir());
    BehaviorTreeRunner::SetAssetBaseDir(m_projectDir);   // a runner's project-relative .okaybt
    Select(nullptr);   // Start()/reset may rebuild; clear m_multi too (no dangling ptrs)
    ActionList::ResetVars();   // clear visual-script variables each Play session
    vs::ForgetSceneVariables(&m_scene);   // scene / global graph variables start empty too
    vs::ClearGlobalVariables();
    // A new session: no stale pause, quit, hit stop or ramp, the time scale back to 1 (Game::Reset kept a
    // scale a script had left, so the next Play started frozen once the editor stepped Time), the clocks
    // at zero and SmoothDelta's history forgotten.
    m_loop.BeginSession();
    m_loop.SetDebugTimeScale(1.0f);   // slow motion is a per-session debugging aid (BeginSession keeps it by design)
    // Audio files named on AudioSources play in Play mode without pressing Load.
    {
        std::vector<std::string> missing;
        LoadSceneAudio(m_scene, m_projectDir, AudioMixer::sampleRate, &missing);
        for (const auto& p : missing) Log::Warning("Audio Source: can't read ", p, " (WAV, OGG, MP3 or FLAC)");
    }
    // Check every visual script first and say what's wrong in the Console
    // (errors in red: that part won't work; warnings: probably not what you meant).
    {
        const auto issues = vs::ValidateSceneGraphs(m_scene, m_projectDir);
        int warnings = 0;
        for (const auto& si : issues) {
            if (si.issue.severity == 0) Log::Error(si.Describe());
            else if (++warnings <= 20) Log::Warning(si.Describe());
        }
        if (warnings > 20) Log::Warning("Visual Script: ", warnings - 20, " more warnings (open the graph to see them all)");
    }
    m_scene.runScripts = true;
    // No m_scene.Start() here: the first Tick's Update starts every object (it
    // flushes the same pending list). That tick runs on the game fiber while a
    // visual-script breakpoint is set, so a breakpoint in On Start can freeze it;
    // starting here, inside the Play button's handler, it couldn't.
    m_playing = true;
}

void EditorState::Stop() {
    if (finishFrozenTick) finishFrozenTick();   // never rebuild a scene frozen mid-frame
    if (!m_playing) return;
    okay::filedialog::CancelBuiltin();   // the deserialize below rebuilds every object (#732)
    m_playing = false;
    m_scene.UseFixedStepPhysics(false);
    m_loop.BeginSession();   // unpause, clear quit and any script's time scale, so the next Play starts clean
    m_loop.SetDebugTimeScale(1.0f);   // and the toolbar's slow motion: a new Play never starts at 0.25x
    ActionList::DebugPaused() = false; ActionList::StepBudget() = 0;   // never leave Actions frozen after Stop
    // Deserialize rebuilds the scene, destroying every live component — including
    // any NetworkManager m_net points at. Drop the pointer first so TickServices
    // never dereferences freed memory (a use-after-free crash).
    m_net = nullptr;
    Matchmaker::Shared().Cancel();   // a search started in play mode ends with it
    m_scene.runScripts = false;
    SceneSerializer::Deserialize(m_scene, m_snapshot); // restore edit state
    for (const auto& up : m_scene.Objects())
        if (auto* ma = up->GetComponent<ModelAnimator>())
            PoseCapture::ImportFolder(*ma, PoseCapture::AssetBaseDir());
    dirty = m_dirtyAtPlay;   // edits made to the running game are thrown away, so they leave no mark
    if (okay::FirstPersonHands::RestoreApplied(m_scene)) dirty = true;   // Apply during Play
    Select(nullptr);   // Deserialize freed every object; clear m_multi too (no dangling ptrs)
}

void EditorState::Tick(float dt) {
    m_scene.runScripts = m_playing;   // edit-mode flushes (UI tools, previews) never run game code
    if (m_playing) {
        m_scene.Update(dt); NavigateUI(m_scene);
        Matchmaker::Shared().Pump(m_scene.FindObjectOfType<NetworkManager>());   // net_queue_join(): connect a found match
    }
}

bool EditorState::PlayFrame(double realSeconds, bool smooth, float refreshHz) {
    if (!m_playing) return false;   // editing never steps Time
    GameLoop::Options o = m_loop.options();
    if (o.smoothDelta != smooth || o.refreshHz != refreshHz) { o.smoothDelta = smooth; o.refreshHz = refreshHz; m_loop.SetOptions(o); }
    // The first frame of a session measures the click frame: Play()'s own work (snapshot, audio loading, validation)
    // plus the editor's UI. Stepping the game by that (up to maxDelta) made the first frame a big jump (five physics
    // steps at once) and put one huge frame into the FPS readout, which then took half a second to recover. A
    // frame longer than 1/30 s here is a hitch of the editor's, not game time: step one 60 Hz frame instead.
    if (m_loop.FrameCount() == 0 && realSeconds > 1.0 / 30.0) realSeconds = 1.0 / 60.0;
    return m_loop.BeginFrame(realSeconds);
}

void EditorState::SetPaused(bool pause) {
    if (!m_playing || pause == paused()) return;
    if (pause) Game::PushPause(kPauseReason); else Game::ClearPause(kPauseReason);
}

bool EditorState::StepFrame(float seconds) {
    if (!m_playing) return false;
    SetPaused(true);
    return m_loop.RequestStep(1, seconds);
}

float EditorState::StepDebugTimeScale(int direction) {
    if (!m_playing) return m_loop.DebugTimeScale();
    m_loop.SetDebugTimeScale(GameLoop::NextDebugTimeScale(m_loop.DebugTimeScale(), direction));
    return m_loop.DebugTimeScale();
}

void EditorState::ResetDebugTimeScale() {
    m_loop.SetDebugTimeScale(1.0f);
}

bool EditorState::RunFrames(int frames, float frameSeconds) {
    if (!m_playing || frames < 1) return false;
    // The loop pauses under the editor's own reason, so the Pause button (Game::PushPause("editor")) and the
    // loop agree when the run ends; already paused, it steps the frames and stays paused.
    return m_loop.RunFrames(frames, frameSeconds, kPauseReason);
}

std::vector<std::string> EditorState::PerfLines() const {
    return m_playing ? FrameOverlay::Lines(m_loop) : std::vector<std::string>();
}

std::vector<std::string> EditorState::SceneLines(int maxComponentTypes) const {
    return m_scene.Stats().Lines(maxComponentTypes);
}

std::vector<std::string> EditorState::StatsLines(int maxComponentTypes) const {
    std::vector<std::string> lines = PerfLines();
    for (std::string& l : SceneLines(maxComponentTypes)) lines.push_back(std::move(l));
    return lines;
}

std::string EditorState::FpsLabel() const {
    return FrameOverlay::FpsLabel(m_loop);
}

std::vector<float> EditorState::RecentFps(std::size_t count) const {
    return FrameOverlay::RecentFps(m_loop.Stats(), count);
}

std::vector<std::string> EditorState::TimerLines(std::size_t maxChars) {
    TimerDebug::Options o;
    o.topGroups = 3;
    o.topOldest = 1;
    // The scene's own timers and tweens (a scene change or Clear drops them), then the process-wide scheduler
    // (Scheduler::Global(), which survives scene loads) when something is in it.
    std::vector<std::string> lines = TimerDebug::BuildLines(&m_scene.scheduler(), &m_scene.tweens(), o);
    if (Scheduler::Global().ActiveCount() > 0) {
        std::vector<std::string> global = TimerDebug::BuildLines(&Scheduler::Global(), nullptr, o);
        if (!global.empty()) global[0] = "Global " + global[0];
        for (std::string& l : global) lines.push_back(std::move(l));
    }
    if (maxChars >= 4)
        for (std::string& l : lines)
            if (l.size() > maxChars) { l.resize(maxChars - 3); l += "..."; }
    return lines;
}


// ======================================================================================================
// Prefab mode
// ======================================================================================================
namespace {

namespace fs = std::filesystem;

bool SameFile(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    std::error_code ec;
    if (fs::equivalent(a, b, ec) && !ec) return true;
    ec.clear();
    return fs::weakly_canonical(a, ec).generic_string() == fs::weakly_canonical(b, ec).generic_string();
}

// A variant file (`variant "<base>"`): opened, edited and saved as a plain prefab it would lose its link to the base.
bool IsVariantFile(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        std::size_t i = line.find_first_not_of(" \t");
        if (i != std::string::npos && line.compare(i, 7, "variant") == 0 && (i + 7 == line.size() || line[i + 7] == ' ')) return true;
    }
    return false;
}

// Does the prefab `source` hold `target` (itself, a nested prefab, a nested prefab of a nested prefab ...)?
bool Reaches(const std::string& source, const std::string& target, std::map<std::string, bool>& cache) {
    auto it = cache.find(source);
    if (it != cache.end()) return it->second;
    bool r = SameFile(source, target);
    if (!r) {
        const okay::PrefabReport rep = okay::PrefabTools::ValidateAsset(source);
        for (const std::string& f : rep.files) if (SameFile(f, target)) { r = true; break; }
    }
    return cache[source] = r;
}

// The outermost instance roots of the scene whose prefab is one of `paths` or holds one (`all`: every instance).
std::vector<okay::GameObject*> InstancesReaching(okay::Scene& scene, const std::vector<std::string>& paths, bool all = false) {
    std::map<std::string, bool> cache;
    std::vector<okay::GameObject*> cands;
    std::set<const okay::GameObject*> set;
    for (const auto& up : scene.Objects()) {
        okay::GameObject* g = up.get();
        if (!g || g->transient || g->IsDestroyed() || g->prefabSource.empty()) continue;
        bool hit = all;
        for (const std::string& p : paths) if (!hit && Reaches(g->prefabSource, p, cache)) hit = true;
        if (hit) { cands.push_back(g); set.insert(g); }
    }
    std::vector<okay::GameObject*> out;
    for (okay::GameObject* g : cands) {   // an instance inside another one is rebuilt with it
        bool inner = false;
        for (okay::Transform* t = g->transform->Parent(); t; t = t->Parent()) if (set.count(t->gameObject)) { inner = true; break; }
        if (!inner) out.push_back(g);
    }
    return out;
}

} // namespace

std::string EditorState::StemOf(const std::string& path) { return fs::path(path).stem().string(); }

GameObject* EditorState::PrefabRootObject() const {
    if (m_levels.empty() || m_ses.rootGuid.empty()) return nullptr;
    GameObject* g = m_scene.FindByGuid(m_ses.rootGuid);
    return g && !g->IsDestroyed() ? g : nullptr;
}

std::vector<EditorState::PrefabCrumb> EditorState::PrefabCrumbs() const {
    std::vector<PrefabCrumb> v;
    for (const Level& lv : m_levels) {
        PrefabCrumb c;
        c.path = lv.ses.path;
        c.label = lv.ses.path.empty() ? (lv.path.empty() ? lv.name : StemOf(lv.path)) : StemOf(lv.ses.path);
        c.unsaved = lv.unsaved;
        v.push_back(std::move(c));
    }
    if (!m_levels.empty()) {
        PrefabCrumb c;
        c.path = m_ses.path;
        c.label = StemOf(m_ses.path);
        c.unsaved = dirty;
        v.push_back(std::move(c));
    }
    return v;
}

std::string EditorState::PlayBlockedReason() const {
    if (m_levels.empty()) return "";
    return "Play is off while a prefab is open. Go Back to the scene to play it.";
}

bool EditorState::PrefabOuterUnsaved() const {
    for (const Level& lv : m_levels) if (lv.unsaved) return true;
    return false;
}

bool EditorState::PrefabUnsavedFrom(int depth) {
    if (m_levels.empty()) return false;
    for (int i = std::max(depth, 0); i < (int)m_levels.size(); ++i) if (m_levels[(std::size_t)i].unsaved) return true;
    return depth <= (int)m_levels.size() && HasUnsavedChanges();
}

// The text of a prefab's editing document: the file instantiated alone, its root made plain (the root IS the
// prefab, not an instance of it). Nested prefabs stay linked instances.
bool EditorState::BuildPrefabText(const std::string& path, std::string& text, std::string& rootGuid, std::string* error) const {
    Scene tmp("prefab");
    tmp.runScripts = false;
    std::string err;
    GameObject* root = PrefabTools::Instantiate(tmp, path, {}, &err);
    if (!root) { if (error) *error = err.empty() ? "the file did not load" : err; return false; }
    root->prefabSource.clear();
    root->prefabOverrides.clear();
    rootGuid = root->Guid();
    text = SceneSerializer::Serialize(tmp);
    return true;
}

bool EditorState::CanOpenPrefab(const std::string& path, std::string* why) const {
    auto no = [&](const std::string& m) { if (why) *why = m; return false; };
    if (m_playing) return no("Stop Play first.");
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec)) return no("The prefab file doesn't exist: " + path);
    if (fs::path(path).extension() != ".okayprefab") return no(fs::path(path).filename().string() + " is not a prefab (.okayprefab).");
    if (IsVariantFile(path)) return no(fs::path(path).filename().string() + " is a variant. Variants keep a link to their base prefab, so they are changed by their overrides (Inspector) or by opening the base prefab.");
    if (!m_levels.empty()) {
        if (SameFile(path, m_ses.path)) return no(fs::path(path).filename().string() + " is the prefab you are editing.");
        for (const Level& lv : m_levels)
            if (!lv.ses.path.empty() && SameFile(path, lv.ses.path)) return no(fs::path(path).filename().string() + " is already open further out (see the bar at the top of the Scene view).");
    }
    std::string text, guid, err;
    if (!BuildPrefabText(path, text, guid, &err)) return no("Couldn't open " + fs::path(path).filename().string() + ": " + err);
    return true;
}

void EditorState::SelectByRef(const SelectionRef& want) {
    Select(nullptr);
    GameObject* prim = nullptr;
    for (const auto& up : m_scene.Objects()) {
        if (up->transient || up->IsDestroyed()) continue;
        const std::string& g = up->Guid();
        if (std::find(want.guids.begin(), want.guids.end(), g) == want.guids.end()) continue;
        if (m_multi.empty()) Select(up.get()); else ToggleSelect(up.get());
        if (g == want.primary) prim = up.get();
    }
    if (prim) m_selected = prim;
}

// Before this document is put aside, make the stored overrides of its instances exact: they are compared with the
// files while the files still say what the instances were made from. Rebuilding an instance later (after a prefab
// was saved) keeps what it changed. Recorded as an undo step so the document's history stays valid.
void EditorState::CaptureInstanceOverrides() {
    SyncHistory();
    const std::vector<GameObject*> inst = InstancesReaching(m_scene, {}, /*all=*/true);
    if (inst.empty()) return;
    const std::string before = SceneSerializer::Serialize(m_scene);
    const bool wasClean = !(dirty || Fingerprint(before) != m_savedPrint);
    for (GameObject* g : inst) PrefabTools::CaptureOverrides(*g);
    const std::string after = SceneSerializer::Serialize(m_scene);
    if (after == before) return;
    RecordStep(before, after, "Record Prefab Overrides", CaptureSelection(), 0);
    m_known = after;
    if (wasClean) MarkClean(); else dirty = true;
}

bool EditorState::OpenPrefab(const std::string& pathIn, std::string* error) {
    const std::string path = pathIn;   // a copy: the caller may hand us an object's own string (OpenPrefabOf), and the objects are rebuilt below
    std::string why;
    if (!CanOpenPrefab(path, &why)) { if (error) *error = why; return false; }
    std::string text, guid, err;
    if (!BuildPrefabText(path, text, guid, &err)) { if (error) *error = err; return false; }
    if (finishFrozenTick) finishFrozenTick();   // never rebuild a scene frozen mid-frame
    CaptureInstanceOverrides();
    SyncHistory();
    // Put the open document aside. The prefab text is applied first: if it fails the scene is as it was.
    Level lv;
    lv.text = SceneSerializer::Serialize(m_scene);
    lv.path = m_path;
    lv.name = m_scene.Name();
    lv.ses = m_ses;
    lv.sel = CaptureSelection();
    lv.cameraPos = cameraPos; lv.cameraZoom = cameraZoom; lv.view3D = view3D;
    lv.camYaw = camYaw; lv.camPitch = camPitch; lv.camDist = camDist; lv.camTarget = camTarget;
    lv.known = m_known;
    lv.savedPrint = m_savedPrint;
    lv.unsaved = dirty || Fingerprint(lv.text) != m_savedPrint;
    StopNetwork();
    if (!SceneSerializer::Deserialize(m_scene, text)) {
        if (error) *error = "Couldn't open " + fs::path(path).filename().string() + ": the prefab did not load into the editor.";
        return false;
    }
    ++m_undoSerial;   // a widget session that was open across this swap (the menu item that opened the prefab) must not record it as an edit
    lv.stack = std::move(m_stack);
    m_stack = std::make_unique<UndoStack>();
    m_stack->SetMaxCommands(kMaxUndo);
    m_stack->SetMergeWindow(1.0f);
    m_levels.push_back(std::move(lv));
    m_ses = PrefabSession{path, guid, {}, PrefabDiskPrint(path), false};
    m_scene.SetName(StemOf(path));
    autoCamera = nullptr;
    m_pending = Pending{};
    m_known = SceneSerializer::Serialize(m_scene);
    m_savedPrint = Fingerprint(m_known);
    m_stack->MarkSaved();
    dirty = false;
    m_prefabNote.clear();
    GameObject* root = PrefabRootObject();
    Select(root);
    if (root) {   // look at it: centre the view on the prefab
        const Vec3 p = root->transform->Position();
        camTarget = p;
        cameraPos = Vec2{p.x, p.y};
    }
    return true;
}

bool EditorState::OpenPrefabOf(GameObject* go, std::string* error) {
    GameObject* r = go ? PrefabTools::NearestPrefabRoot(*go) : nullptr;
    if (!r) { if (error) *error = "That object is not a prefab instance."; return false; }
    return OpenPrefab(r->prefabSource, error);
}

std::uint64_t EditorState::PrefabDiskPrint(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;
    std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return Fingerprint(t) | 1;   // never 0: 0 means "no file"
}

bool EditorState::PrefabChangedOnDisk() const { return !m_levels.empty() && PrefabDiskPrint(m_ses.path) != m_ses.disk; }

bool EditorState::SavePrefab(std::string* error, std::string* warning) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (m_levels.empty()) return fail("No prefab is open.");
    GameObject* root = PrefabRootObject();
    if (!root) return fail("The prefab's root object was deleted, so there is nothing to save. Undo the delete, or go Back and discard.");
    if (PrefabChangedOnDisk() && !m_ses.overwriteArmed) {   // never replace someone else's change without saying so
        m_ses.overwriteArmed = true;
        return fail(fs::path(m_ses.path).filename().string() + " was changed outside the editor since you opened it, so saving would replace that change. Nothing was written. Save again to overwrite the file with your version, or Revert to load the file as it is now.");
    }
    int outside = 0;
    for (const auto& up : m_scene.Objects())
        if (!up->transient && !up->IsDestroyed() && up.get() != root && !up->transform->Parent()) ++outside;
    std::string err;
    if (!PrefabTools::SavePrefab(*root, m_ses.path, &err)) return fail("Couldn't save " + fs::path(m_ses.path).filename().string() + ": " + err);
    if (std::find(m_ses.saved.begin(), m_ses.saved.end(), m_ses.path) == m_ses.saved.end()) m_ses.saved.push_back(m_ses.path);
    m_ses.disk = PrefabDiskPrint(m_ses.path);
    m_ses.overwriteArmed = false;
    MarkClean();
    if (warning && outside)
        *warning = std::to_string(outside) + (outside == 1 ? " object sits" : " objects sit") + " outside the prefab's root and was not saved. Drag it under the root to include it.";
    return true;
}

bool EditorState::RevertPrefab(std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (m_levels.empty()) return fail("No prefab is open.");
    std::string text, guid, err;
    if (!BuildPrefabText(m_ses.path, text, guid, &err)) return fail("Couldn't read " + fs::path(m_ses.path).filename().string() + ": " + err);
    if (finishFrozenTick) finishFrozenTick();
    PushUndo("Revert Prefab");   // closes the step before it; the reload below is one step of its own
    StopNetwork();
    if (!SceneSerializer::Deserialize(m_scene, text)) return fail("The file did not load.");
    if (GameObject* nr = m_scene.FindByGuid(guid)) nr->SetGuid(m_ses.rootGuid);   // the root keeps its id, so Undo of the revert finds it again
    m_scene.SetName(StemOf(m_ses.path));
    Select(PrefabRootObject());
    SyncHistory();                                  // records "Revert Prefab" (nothing when there was nothing to revert)
    m_ses.disk = PrefabDiskPrint(m_ses.path);       // the file as it is now is what was read
    m_ses.overwriteArmed = false;
    m_savedPrint = Fingerprint(m_known);            // what is on screen is the file
    m_stack->MarkSaved();
    dirty = false;
    return true;
}

bool EditorState::ClosePrefab(bool save, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (m_levels.empty()) return fail("No prefab is open.");
    SyncHistory();
    if (save && !SavePrefab(error)) return false;
    const std::vector<std::string> saved = m_ses.saved;   // what was written here, and in the levels opened from here
    if (finishFrozenTick) finishFrozenTick();
    StopNetwork();
    if (!SceneSerializer::Deserialize(m_scene, m_levels.back().text)) return fail("Couldn't go back to " + (m_levels.back().ses.path.empty() ? std::string("the scene") : StemOf(m_levels.back().ses.path)) + ".");
    Level lv = std::move(m_levels.back());
    m_levels.pop_back();
    ++m_undoSerial;
    m_stack = std::move(lv.stack);
    m_pending = Pending{};
    m_known = SceneSerializer::Serialize(m_scene);
    m_savedPrint = lv.savedPrint;
    dirty = lv.unsaved;
    m_path = lv.path;
    m_scene.SetName(lv.name);
    m_ses = lv.ses;
    for (const std::string& p : saved) if (std::find(m_ses.saved.begin(), m_ses.saved.end(), p) == m_ses.saved.end()) m_ses.saved.push_back(p);   // the levels further out still owe an update
    cameraPos = lv.cameraPos; cameraZoom = lv.cameraZoom; view3D = lv.view3D;
    camYaw = lv.camYaw; camPitch = lv.camPitch; camDist = lv.camDist; camTarget = lv.camTarget;
    autoCamera = nullptr;
    SelectByRef(lv.sel);
    m_prefabNote.clear();
    if (!saved.empty()) RefreshInstancesOf(saved, &m_prefabNote);
    return true;
}

bool EditorState::ClosePrefabTo(int depth, bool save, std::string* error) {
    if (depth < 0) depth = 0;
    if (depth > (int)m_levels.size()) { if (error) *error = "That level is not open."; return false; }
    while ((int)m_levels.size() > depth)
        if (!ClosePrefab(save, error)) return false;
    return true;
}

int EditorState::RefreshPrefabInstances(const std::string& path, std::string* report) {
    return RefreshInstancesOf({path}, report);
}

int EditorState::RefreshInstancesOf(const std::vector<std::string>& paths, std::string* report) {
    SyncHistory();
    const std::vector<GameObject*> inst = InstancesReaching(m_scene, paths);
    if (inst.empty()) { if (report) report->clear(); return 0; }
    const std::string before = SceneSerializer::Serialize(m_scene);
    const SelectionRef sel = CaptureSelection();
    int rebuilt = 0, failed = 0;
    std::string firstError;
    std::vector<std::string> warnings;
    for (GameObject* g : inst) {
        std::string err;
        if (PrefabTools::Reapply(m_scene, *g, &err, &warnings)) ++rebuilt;   // keeps the name, parent, place and ids
        else { ++failed; if (firstError.empty()) firstError = err; }
    }
    m_scene.FlushDestroyed();   // the old instances are gone now, so the text below does not hold two
    const std::string after = SceneSerializer::Serialize(m_scene);
    if (after != before) {
        RecordStep(before, after, "Update Prefab Instances", sel, 0);
        m_known = after;
        dirty = true;
    }
    SelectByRef(sel);
    if (report) {
        std::string names;
        for (const std::string& p : paths) names += (names.empty() ? "" : ", ") + StemOf(p);
        *report = "Updated " + std::to_string(rebuilt) + (rebuilt == 1 ? " instance" : " instances") + " of " + names;
        if (failed) *report += "; " + std::to_string(failed) + " could not be rebuilt (" + firstError + ")";
        else if (!warnings.empty()) *report += " (" + warnings.front() + (warnings.size() > 1 ? ", +" + std::to_string(warnings.size() - 1) + " more" : std::string()) + ")";
    }
    return rebuilt;
}

bool EditorState::SaveAllPrefabLevels(std::string* error, int fromDepth) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (m_levels.empty()) return true;
    if (HasUnsavedChanges()) { if (!SavePrefab(error)) return false; }
    for (int i = (int)m_levels.size() - 1; i >= std::max(fromDepth, 0); --i) {   // innermost first
        Level& lv = m_levels[(std::size_t)i];
        if (!lv.unsaved) continue;
        if (!lv.ses.path.empty() && PrefabDiskPrint(lv.ses.path) != lv.ses.disk)
            return fail(fs::path(lv.ses.path).filename().string() + " was changed outside the editor, so it was not overwritten. Go back to it and Save (twice to overwrite) or Revert.");
        Scene tmp("level");
        tmp.runScripts = false;
        if (!SceneSerializer::Deserialize(tmp, lv.text)) return fail("Couldn't read the put-aside " + (lv.ses.path.empty() ? std::string("scene") : StemOf(lv.ses.path)) + " to save it.");
        std::string err;
        if (lv.ses.path.empty()) {
            if (lv.path.empty()) return fail("The scene has no file yet. Go Back to the scene and use Save Scene As.");
            if (!SceneSerializer::SaveToFile(tmp, lv.path, &err)) return fail("Couldn't save " + fs::path(lv.path).filename().string() + ": " + err);
        } else {
            GameObject* root = tmp.FindByGuid(lv.ses.rootGuid);
            if (!root) return fail("The root of " + StemOf(lv.ses.path) + " was deleted; nothing to save.");
            if (!PrefabTools::SavePrefab(*root, lv.ses.path, &err)) return fail("Couldn't save " + fs::path(lv.ses.path).filename().string() + ": " + err);
            if (std::find(lv.ses.saved.begin(), lv.ses.saved.end(), lv.ses.path) == lv.ses.saved.end()) lv.ses.saved.push_back(lv.ses.path);
            lv.ses.disk = PrefabDiskPrint(lv.ses.path);
        }
        lv.savedPrint = Fingerprint(lv.text);
        lv.unsaved = false;
        lv.stack->MarkSaved();
    }
    return true;
}

} // namespace okay::editor
