#pragma once
#include <cstdint>
#include <functional>
#include <Okay.hpp>
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
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
    /// Hierarchy "Delete Children": destroy every child (and what is below them) of the selected object(s) and
    /// keep the objects themselves selected; one undo step. Returns how many direct children were removed
    /// (0, and no undo step, when nothing selected has a child).
    int DeleteChildrenOfSelected();
    /// Clone the selected object(s) (and their children); selects the clones.
    GameObject* DuplicateSelected();
    /// Move a just-created object onto open ground near the scene-view focus.
    /// Call it in the same undo step that created the object.
    GameObject* SettleSpawn(GameObject* go);
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
    /// Write the scene to `path` (atomically: see SceneSerializer::SaveToFile). Refused while Play is
    /// running: the scene then is the running game, not what you authored. On success the scene counts
    /// as saved; on failure nothing changes and `error` says why.
    bool Save(const std::string& path, std::string* error = nullptr);
    /// Write meshes edited since they were loaded from a .okaymesh file back to
    /// that file (Save does this first). Returns how many files were written.
    int SaveEditedMeshAssets(std::string* report = nullptr);
    /// Called for an object whose mesh was replaced by its re-saved asset (same
    /// vertices, triangles possibly re-ordered): mesh edit mode drops face/edge selections.
    static inline std::function<void(GameObject*)> onMeshTrianglesReordered;
    /// Open a scene file. All or nothing: a missing or damaged file leaves the open scene, its undo
    /// history and its saved state exactly as they were (`error` says why). A loaded scene starts a
    /// fresh undo history, so Undo can never put the old scene's objects into the new file.
    bool Load(const std::string& path, std::string* error = nullptr);
    /// Load scene text recovered from an autosave. The scene's path stays `keepPath`
    /// and the scene is left unsaved, so the real file is not overwritten.
    bool LoadRecoveredText(const std::string& text, const std::string& keepPath, std::string* error = nullptr);
    const std::string& path() const { return m_path; }
    /// The open scene's file was renamed or moved: keep editing it under the new path.
    void FollowMovedFile(const std::string& newPath) { m_path = newPath; }
    /// An asset the scene uses was renamed or moved: pass the scene's text (live scene, undo and redo history
    /// and the saved copy) through `rewrite`. A scene that had no unsaved changes still has none. Returns
    /// true when the live scene changed.
    bool RewriteAssetPaths(const std::function<std::string(const std::string&)>& rewrite);

    // ---- Unsaved changes --------------------------------------------------------
    /// The scene as it is now counts as saved: clears `dirty` and remembers what the scene looks like.
    void MarkClean();
    /// True when the open scene differs from what was last saved, loaded or created. `dirty` says so
    /// cheaply, but an edit that forgot to raise it would be lost silently, so this also compares the
    /// scene's saved text with the text at the last save (it serializes the scene: call it when a
    /// decision depends on it, such as quitting or opening another scene, not every frame). While Play
    /// is running it answers for the scene you are editing: changes made to the running game are
    /// thrown away on Stop and never count.
    bool HasUnsavedChanges();
    /// Same as HasUnsavedChanges, and raises `dirty` when the text differs (so the title bar and the
    /// autosave catch up with an edit that did not mark it). Cheap enough to call every few seconds.
    bool SyncDirty();

    // ---- A new project: all or nothing -------------------------------------------
    struct NewProjectResult {
        bool ok = false;
        std::string root;    ///< the project folder
        std::string scene;   ///< its first scene file
        std::string error;   ///< why it failed; the open scene, project and files are as before
    };
    /// Make a project at `root`: its folders, then `writeFiles(root, err)` (the caller's own settings files),
    /// then the template scene (`tpl`) saved as <root>/Assets/<sceneName>.okayscene, and open it. If any step
    /// fails everything made is removed and the scene, path, project and undo history are put back, so a
    /// failed Create never costs the open scene. `writeFiles` may be empty.
    NewProjectResult CreateProject(const std::string& root, const std::string& sceneName, void (EditorState::*tpl)(),
                                   const std::function<bool(const std::string& root, std::string& error)>& writeFiles = {});
    /// Called when the project folder changes (open, create, close): the old folder, then the new one.
    /// The editor uses it to save the old project's settings and load the new one's.
    static inline std::function<void(const std::string& oldDir, const std::string& newDir)> onProjectChanged;
    /// Called after a scene file was opened successfully (the editor clears its "last save failed" note).
    static inline std::function<void()> onSceneLoaded;
    /// Asked before Load touches anything: return false (with the reason in `why`) to refuse the file. The editor puts
    /// the project version gate here, so a scene of a project nobody has answered the version question for can't be
    /// opened by a path that skipped it. Unset (tests, tools): every file may be opened.
    static inline std::function<bool(const std::string& path, std::string* why)> beforeLoad;

    // ---- Project (a folder on disk holding the scenes + assets) ---------
    const std::string& projectDir() const { return m_projectDir; }
    /// Make `d` the open project. Only a real change does anything: the item definitions and the string
    /// table are that project's own and are read again, and onProjectChanged runs. Setting the folder
    /// that is already open leaves everything (including unsaved edits to those tables) alone.
    void setProjectDir(const std::string& d) {
        if (d == m_projectDir) return;
        const std::string old = m_projectDir;
        m_projectDir = d;
        if (onProjectChanged) onProjectChanged(old, d);
        okay::ItemDatabase::Get().Clear();   // each project has its own item definitions
        if (!d.empty()) okay::ItemDatabase::Get().Load(ItemDatabasePath());
        okay::Localization::Reset();         // ... and its own string table
        if (!d.empty()) okay::Localization::LoadTable(StringTablePath());
    }
    /// The project's item definitions file (<project>/items.okayitems).
    std::string ItemDatabasePath() const { return m_projectDir.empty() ? std::string() : m_projectDir + "/items.okayitems"; }
    /// The project's string table (<project>/strings.okayloc; the Localization window).
    std::string StringTablePath() const { return m_projectDir.empty() ? std::string() : m_projectDir + "/strings.okayloc"; }

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

    // ---- The Play loop (okay/Core/GameLoop.hpp) ------------------------
    // Play frames go through one GameLoop, as in the built player, so Time::DeltaTime(),
    // ElapsedTime(), Time.timeScale, hit stops, time-scale ramps and the Clock channels all
    // work during Play. Editing never steps Time: PlayFrame does nothing outside Play.
    /// Run one frame of Play for `realSeconds` of real time: clamp it to 0.1 s (a compile or a
    /// window drag is not one huge step), optionally even it out (Time::SmoothDelta, snapped to
    /// `refreshHz` with vsync), step Time and the clocks, then run playTick with the scaled
    /// delta. False when nothing ran (not playing) or the game asked to quit.
    bool PlayFrame(double realSeconds, bool smooth = true, float refreshHz = 0.0f);
    /// What a Play frame runs with Time::DeltaTime(). Empty means Tick(dt); the editor sets it
    /// so the visual-script debugger can run the tick on its own fiber.
    std::function<void(float)> playTick;
    /// The editor's Pause button, held as Game::PushPause("editor") so the loop, the clocks and
    /// the game's own pause menu agree. Paused, Time::DeltaTime() is 0 and the scene is not
    /// updated (as before). Ignored outside Play; Play() and Stop() clear it.
    void SetPaused(bool paused);
    bool paused() const { return Game::IsPausedBy(kPauseReason); }
    /// The Step button: pause, then advance exactly one frame of `seconds` at the next
    /// PlayFrame. The scene sees a running game for that frame (Game::Paused() reads false for the
    /// editor's own pause, so controllers that skip a paused game still move); the time scale and
    /// hit stops apply. False when not playing.
    bool StepFrame(float seconds = 1.0f / 60.0f);
    /// The loop itself (statistics, debug time scale, capture mode, frame recording).
    GameLoop& playLoop() { return m_loop; }
    const GameLoop& playLoop() const { return m_loop; }
    static constexpr const char* kPauseReason = "editor";

    // ---- Debug controls on the Play loop (the toolbar's slow-motion and "run N frames" buttons) --------
    /// The slow-motion / fast-forward factor of the Play loop (1 = normal). It is a debugging aid, not a
    /// game setting: only the editor's frame time is scaled, the game's own Time.timeScale is untouched.
    float debugTimeScale() const { return m_loop.DebugTimeScale(); }
    /// The "Slower" (`direction` < 0) and "Faster" (> 0) buttons: move to the next preset
    /// (GameLoop::kDebugScalePresets: 0.05 ... 1 ... 32). Returns the scale now in force. Does nothing
    /// outside Play (the buttons are greyed out then).
    float StepDebugTimeScale(int direction);
    /// The "1x" button.
    void ResetDebugTimeScale();
    /// "Run N frames, then pause": runs exactly `frames` more frames of `frameSeconds` each (the same
    /// frame length every time, so a bug reproduces), then holds the editor's own pause. Paused already,
    /// it steps `frames` frames and stays paused. The pause button follows through `onRunFramesDone`.
    /// False, nothing changed, outside Play or for `frames` < 1.
    bool RunFrames(int frames, float frameSeconds = 1.0f / 60.0f);
    /// Called (main thread, end of the frame) when a run started by RunFrames has paused the game: the
    /// editor sets its Pause button flag here so the button and the loop agree.
    std::function<void()> onRunFramesDone;
    /// Frames still queued by Step or RunFrames (0 = none): the Pause button shows the number.
    int PendingFrames() const { return m_loop.StepsRemaining() + m_loop.RunFramesRemaining(); }

    // ---- Text for the Game view's overlays (ImGui-free, so it can be tested) --------------------------
    /// The Stats overlay: while playing, the performance lines of the Play loop (FrameOverlay::Lines: fps,
    /// frame times, 1% low, hitches, time scale, pause reasons) followed by the scene's counts
    /// (Scene::Stats().Lines(maxComponentTypes)); in edit mode only the counts (nothing is running).
    std::vector<std::string> StatsLines(int maxComponentTypes = 3) const;
    /// The two halves of StatsLines, for a UI that draws something between them (the fps graph) or leaves one
    /// out: the loop's performance lines (empty outside Play) and the scene's counts.
    std::vector<std::string> PerfLines() const;
    std::vector<std::string> SceneLines(int maxComponentTypes = 3) const;
    /// One line for the Game view's corner, beside the PLAYING pill (FrameOverlay::FpsLabel: the real frame times of
    /// the last 30 frames, so a stall reads as a stall; "FPS --" before the first frame).
    std::string FpsLabel() const;
    /// The recent frame rate for the overlay's graph, `count` points, oldest first (all 0 until frames ran).
    std::vector<float> RecentFps(std::size_t count) const;
    /// The Timers overlay: what the scene's scheduler and tweens are doing (TimerDebug::BuildLines), plus
    /// the process-wide scheduler when it holds anything. Long lines are shortened to `maxChars`.
    std::vector<std::string> TimerLines(std::size_t maxChars = 78);

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

    GameLoop m_loop;   // the Play loop (set up in the constructor)

    std::vector<std::string> m_undo, m_redo;
    std::uint64_t m_savedPrint = 0;       // fingerprint of the scene text at the last save / load / new
    std::uint64_t m_playPrint = 0;        // fingerprint of the edit scene Play() put aside
    bool m_dirtyAtPlay = false;           // `dirty` when Play started (Play-time edits are thrown away)
    static std::uint64_t Fingerprint(const std::string& sceneText);
    bool m_suppressUndo = false;
    static constexpr std::size_t kMaxUndo = 100;
};

} // namespace okay::editor
