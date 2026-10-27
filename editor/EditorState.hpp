#pragma once
#include <functional>
#include <Okay.hpp>
#include <string>
#include <vector>
#include <algorithm>

namespace okay::editor {

/// All non-GUI state and operations of the editor: the scene being edited, the
/// current selection, play/stop with snapshot restore, and an editor camera.
/// Kept free of ImGui/SDL so it can be unit-tested headlessly.
class EditorState {
public:
    EditorState();

    Scene& scene() { return m_scene; }
    const Scene& scene() const { return m_scene; }

    // ---- Selection -----------------------------------------------------
    // m_selected is the PRIMARY selection (what the Inspector shows). m_multi holds
    // every selected object for bulk ops (activate/delete/duplicate). Select() picks
    // a single object; ToggleSelect() (Ctrl-click) adds/removes one from the set.
    GameObject* selected() const { return m_selected; }
    void Select(GameObject* go) {
        m_selected = go; m_multi.clear();
        if (go) m_multi.push_back(go);
    }
    void ToggleSelect(GameObject* go) {
        if (!go) return;
        auto it = std::find(m_multi.begin(), m_multi.end(), go);
        if (it != m_multi.end()) {
            m_multi.erase(it);
            m_selected = m_multi.empty() ? nullptr : m_multi.back();
        } else {
            m_multi.push_back(go);
            m_selected = go;
        }
    }
    const std::vector<GameObject*>& MultiSelection() const { return m_multi; }
    bool IsSelected(GameObject* go) const {
        return go && std::find(m_multi.begin(), m_multi.end(), go) != m_multi.end();
    }
    /// Drop a destroyed object from the selection so nothing dangles.
    void Deselect(GameObject* go) {
        m_multi.erase(std::remove(m_multi.begin(), m_multi.end(), go), m_multi.end());
        if (m_selected == go) m_selected = m_multi.empty() ? nullptr : m_multi.back();
    }

    // ---- Authoring -----------------------------------------------------
    GameObject* CreateEmpty(const std::string& name = "GameObject");
    GameObject* CreateSprite(const std::string& name = "Sprite");
    GameObject* CreateCamera(const std::string& name = "Camera");
    /// Make `cam` the camera the Game view and builds render through (clears the
    /// main flag on the others), and remove the placeholder camera the editor
    /// added to a camera-less scene if it's still untouched.
    void AdoptCamera(Camera* cam);
    /// The "Main Camera" the editor added automatically because the scene had
    /// none (null once it's gone or the user adds their own).
    GameObject* autoCamera = nullptr;
    GameObject* CreateCube(const std::string& name = "Cube");
    GameObject* CreatePyramid(const std::string& name = "Pyramid");
    /// Create a GameObject with a MeshRenderer for any named primitive
    /// ("Cube"/"Pyramid"/"Plane"/"Sphere"/"Cylinder").
    GameObject* CreateMesh(const std::string& meshName);
    void DeleteSelected();
    /// Clone the selected object(s) (and their children); selects the clones.
    GameObject* DuplicateSelected();
    /// Wrap the current selection in a new empty parent,
    /// keeping each object's world position. Returns the new group (selected), or null
    /// if nothing is selected. Only top-level selected objects are reparented.
    GameObject* GroupSelected(const std::string& name = "Group");
    void NewScene();
    /// New project templates: a 2D scene (ortho camera + sprite) or a 3D scene
    /// (perspective camera + ground + cube).
    void NewScene2D();
    void NewScene3D();
    /// Starter templates that build a small playable scene to learn from.
    void NewPlatformer();
    void NewPlatformer3D();
    void NewFPS();
    void NewThirdPerson();
    void NewHumanoid();
    void NewThirdPersonShooter();
    void NewPointAndClick();
    void NewMultiplayer();
    void NewTopDown();
    void NewTopDownShooter();
    void NewPlatformer25D();
    void NewVehicle3D();
    void NewVehicle2D();
    void NewCoinCollector();
    void NewMainMenu();
    void NewSnake();
    void NewBrickBreaker();
    void NewInventory();
    void NewTerrainSandbox();
    void NewVoxelSandbox();

    // ---- Files ---------------------------------------------------------
    bool Save(const std::string& path);
    /// Write meshes edited since they were loaded from a .okaymesh file back to
    /// that file (Save does this first). Returns how many files were written.
    int SaveEditedMeshAssets(std::string* report = nullptr);
    /// Called for an object whose mesh was replaced by its re-saved asset (same
    /// vertices, triangles possibly re-ordered): mesh edit mode drops face/edge selections.
    static inline std::function<void(GameObject*)> onMeshTrianglesReordered;
    bool Load(const std::string& path, std::string* error = nullptr);
    const std::string& path() const { return m_path; }

    // ---- Project (a folder on disk holding the scenes + assets) ---------
    const std::string& projectDir() const { return m_projectDir; }
    void setProjectDir(const std::string& d) {
        m_projectDir = d;
        okay::ItemDatabase::Get().Clear();   // each project has its own item definitions
        if (!d.empty()) okay::ItemDatabase::Get().Load(ItemDatabasePath());
    }
    /// The project's item definitions file (<project>/items.okayitems).
    std::string ItemDatabasePath() const { return m_projectDir.empty() ? std::string() : m_projectDir + "/items.okayitems"; }

    // ---- Online services (built into the engine) ----------------------
    ISteamService*   steam()   { return m_steam.get(); }
    NetworkManager*  net()     { return m_net; }

    /// Host a multiplayer session on the given port (creates a NetworkManager).
    bool StartHost(std::uint16_t port);
    /// Join a multiplayer session.
    bool StartJoin(const std::string& host, std::uint16_t port);
    void StopNetwork();

    /// Pump Steam callbacks and the network each editor frame.
    void TickServices(float dt);
    /// Unlock a Steam achievement (no-op if Steam is unavailable).
    void Achievement(const std::string& id);

    // ---- Undo / Redo (snapshot based) ---------------------------------
    /// Capture the current scene as an undo checkpoint (call before a change).
    void PushUndo();
    bool Undo();
    bool Redo();
    /// Load a scene snapshot, keeping the same objects selected afterwards.
    void RestoreKeepingSelection(const std::string& snapshot);
    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    int  UndoDepth() const { return (int)m_undo.size(); }   ///< steps available behind
    int  RedoDepth() const { return (int)m_redo.size(); }   ///< steps available ahead

    // ---- Play mode -----------------------------------------------------
    bool isPlaying() const { return m_playing; }
    /// Snapshot the scene, then begin running its lifecycle.
    void Play();
    /// Restore the pre-play snapshot and return to editing.
    void Stop();
    /// Set by the editor: when the game is frozen mid-frame at a visual-script
    /// breakpoint, finish that frame (breakpoints off). Called before anything that
    /// rebuilds or deletes from the scene (Stop, undo, delete, new / load scene).
    static inline std::function<void()> finishFrozenTick;
    /// Advance the simulation by dt (only has effect while playing).
    void Tick(float dt);

    // ---- 2D editor camera (world units) -------------------------------
    Vec2  cameraPos{0, 0};
    float cameraZoom = 18.0f; // world units visible vertically

    // ---- 3D editor camera (orbit) -------------------------------------
    bool  view3D    = false;
    float camYaw    = 35.0f;   // degrees
    float camPitch  = 22.0f;   // degrees
    float camDist   = 14.0f;   // distance from target
    Vec3  camTarget = Vec3::Zero;

    bool dirty = false; // unsaved changes

private:
    Scene m_scene;
    GameObject* m_selected = nullptr;
    std::vector<GameObject*> m_multi;   // full selection set (includes m_selected)
    std::string m_path;
    std::string m_projectDir;   // root folder of the open project (with /Assets)
    bool  m_playing = false;
    std::string m_snapshot;

    std::unique_ptr<ISteamService>   m_steam;
    NetworkManager* m_net = nullptr; // component on a scene GameObject

    std::vector<std::string> m_undo, m_redo;
    bool m_suppressUndo = false;
    static constexpr std::size_t kMaxUndo = 100;
};

} // namespace okay::editor
