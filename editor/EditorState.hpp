#pragma once
#include <Okay.hpp>
#include <string>

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
    GameObject* selected() const { return m_selected; }
    void Select(GameObject* go) { m_selected = go; }

    // ---- Authoring -----------------------------------------------------
    GameObject* CreateEmpty(const std::string& name = "GameObject");
    GameObject* CreateSprite(const std::string& name = "Sprite");
    GameObject* CreateCamera(const std::string& name = "Camera");
    GameObject* CreateCube(const std::string& name = "Cube");
    GameObject* CreatePyramid(const std::string& name = "Pyramid");
    void DeleteSelected();
    /// Clone the selected object (and its children); selects the clone.
    GameObject* DuplicateSelected();
    void NewScene();
    /// New project templates: a 2D scene (ortho camera + sprite) or a 3D scene
    /// (perspective camera + ground + cube).
    void NewScene2D();
    void NewScene3D();

    // ---- Files ---------------------------------------------------------
    bool Save(const std::string& path);
    bool Load(const std::string& path, std::string* error = nullptr);
    const std::string& path() const { return m_path; }

    // ---- Online services (built into the engine) ----------------------
    ISteamService*   steam()   { return m_steam.get(); }
    IPlayFabService* playfab() { return m_playfab.get(); }
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

    // ---- Play mode -----------------------------------------------------
    bool isPlaying() const { return m_playing; }
    /// Snapshot the scene, then begin running its lifecycle.
    void Play();
    /// Restore the pre-play snapshot and return to editing.
    void Stop();
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
    std::string m_path;
    bool  m_playing = false;
    std::string m_snapshot;

    std::unique_ptr<ISteamService>   m_steam;
    std::unique_ptr<IPlayFabService> m_playfab;
    NetworkManager* m_net = nullptr; // component on a scene GameObject
};

} // namespace okay::editor
