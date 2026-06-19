#include "EditorState.hpp"

namespace okay::editor {

EditorState::EditorState() : m_scene("Untitled") {
    // Online services are part of the engine: simulation backends by default,
    // real Steamworks / PlayFab when the engine is built with those flags.
    m_steam = CreateSteamService();
    SteamConfig sc; sc.appId = 480; // Spacewar test app id
    if (m_steam) m_steam->Initialize(sc);

    m_playfab = CreatePlayFabService();
    PlayFabConfig pf; pf.titleId = "OKAYDEMO";
    if (m_playfab) m_playfab->Initialize(pf);
}

bool EditorState::StartHost(std::uint16_t port) {
    StopNetwork();
    GameObject* go = m_scene.CreateGameObject("__Network");
    m_net = go->AddComponent<NetworkManager>();
    m_net->SetLocalAvatar(go->transform, '@');
    m_net->SetRemoteFactory([this](std::uint32_t id, char) {
        return m_scene.CreateGameObject("Peer" + std::to_string(id));
    });
    return m_net->StartServer(port);
}

bool EditorState::StartJoin(const std::string& host, std::uint16_t port) {
    StopNetwork();
    GameObject* go = m_scene.CreateGameObject("__Network");
    m_net = go->AddComponent<NetworkManager>();
    m_net->SetLocalAvatar(go->transform, '@');
    m_net->SetRemoteFactory([this](std::uint32_t id, char) {
        return m_scene.CreateGameObject("Peer" + std::to_string(id));
    });
    return m_net->StartClient(host, port);
}

void EditorState::StopNetwork() {
    if (!m_net) return;
    GameObject* go = m_net->gameObject;
    m_net->Stop();
    if (go) { m_scene.Destroy(go); m_scene.Update(0.0f); }
    m_net = nullptr;
}

void EditorState::TickServices(float dt) {
    if (m_steam) m_steam->RunCallbacks();
    if (m_net) m_net->Update(dt);
}

void EditorState::Achievement(const std::string& id) {
    if (m_steam) m_steam->UnlockAchievement(id);
}

void EditorState::PushUndo() {
    if (m_suppressUndo) return;
    m_undo.push_back(SceneSerializer::Serialize(m_scene));
    if (m_undo.size() > kMaxUndo) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

bool EditorState::Undo() {
    if (m_undo.empty()) return false;
    m_redo.push_back(SceneSerializer::Serialize(m_scene));
    std::string s = m_undo.back(); m_undo.pop_back();
    StopNetwork();
    SceneSerializer::Deserialize(m_scene, s);
    m_selected = nullptr;
    dirty = true;
    return true;
}

bool EditorState::Redo() {
    if (m_redo.empty()) return false;
    m_undo.push_back(SceneSerializer::Serialize(m_scene));
    std::string s = m_redo.back(); m_redo.pop_back();
    StopNetwork();
    SceneSerializer::Deserialize(m_scene, s);
    m_selected = nullptr;
    dirty = true;
    return true;
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
    go->AddComponent<Camera>();
    m_selected = go;
    dirty = true;
    return go;
}

GameObject* EditorState::CreateCube(const std::string& name) {
    PushUndo();
    GameObject* go = m_scene.CreateGameObject(name);
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube();
    mr->color = Color::Cyan;
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
    mr->color = Color::Yellow;
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
    mr->color = Color::Cyan;
    m_selected = go;
    view3D = true;
    dirty = true;
    return go;
}

GameObject* EditorState::DuplicateSelected() {
    PushUndo();
    if (!m_selected) return nullptr;
    GameObject* clone = m_scene.Instantiate(*m_selected);
    if (clone) {
        clone->transform->localPosition += Vec3{0.5f, 0.5f, 0.0f}; // offset so it's visible
        m_selected = clone;
        dirty = true;
    }
    return clone;
}

void EditorState::DeleteSelected() {
    PushUndo();
    if (!m_selected) return;
    m_scene.Destroy(m_selected);
    m_scene.Update(0.0f); // flush the destroy queue immediately
    m_selected = nullptr;
    dirty = true;
}

void EditorState::NewScene() {
    PushUndo();
    if (m_playing) Stop();
    StopNetwork();
    m_scene.Clear();
    m_scene.SetName("Untitled");
    m_selected = nullptr;
    m_path.clear();
    dirty = false;
}

void EditorState::NewScene2D() {
    NewScene();
    m_suppressUndo = true; // batch the template objects into one undo step
    m_scene.SetName("Untitled 2D");
    auto* cam = CreateCamera("MainCamera");
    cam->GetComponent<Camera>()->projection = Camera::Projection::Orthographic;
    GameObject* sp = CreateSprite("Sprite");
    sp->GetComponent<SpriteRenderer>()->color = Color::Green;
    m_suppressUndo = false;
    view3D = false;
    m_selected = sp;
    dirty = false;
}

void EditorState::NewScene3D() {
    NewScene();
    m_suppressUndo = true;
    m_scene.SetName("Untitled 3D");
    auto* camObj = CreateCamera("MainCamera");
    camObj->GetComponent<Camera>()->projection = Camera::Projection::Perspective;
    camObj->transform->localPosition = {0, 2, 10};

    // A single cube on the grid (clean, Unity-like default). Add a ground/other
    // objects from the GameObject menu as needed.
    GameObject* cube = CreateCube("Cube");
    cube->transform->localPosition = {0, 0, 0};
    cube->GetComponent<MeshRenderer>()->color = Color::Cyan;

    // An angled directional light so the cube is shaded out of the box.
    GameObject* light = CreateEmpty("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({50, -30, 0});

    // Frame the editor orbit camera on the cube.
    camTarget = {0, 0, 0};
    camDist = 6.0f;

    m_suppressUndo = false;
    view3D = true;
    m_selected = cube;
    dirty = false;
}

void EditorState::NewPlatformer() {
    NewScene();
    m_suppressUndo = true;
    Templates::Platformer(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Player");
    dirty = false;
}

void EditorState::NewTopDown() {
    NewScene();
    m_suppressUndo = true;
    Templates::TopDown(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Player");
    dirty = false;
}

void EditorState::NewCoinCollector() {
    NewScene();
    m_suppressUndo = true;
    Templates::CoinCollector(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Player");
    dirty = false;
}

void EditorState::NewMainMenu() {
    NewScene();
    m_suppressUndo = true;
    Templates::MainMenu(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("StartButton");
    dirty = false;
}

void EditorState::NewSnake() {
    NewScene();
    m_suppressUndo = true;
    Templates::Snake(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Board");
    dirty = false;
}

bool EditorState::Save(const std::string& path) {
    if (!SceneSerializer::SaveToFile(m_scene, path)) return false;
    m_path = path;
    dirty = false;
    return true;
}

bool EditorState::Load(const std::string& path, std::string* error) {
    if (m_playing) Stop();
    PushUndo();
    StopNetwork();
    if (!SceneSerializer::LoadFromFile(m_scene, path, error)) return false;
    m_path = path;
    m_selected = nullptr;
    dirty = false;
    return true;
}

void EditorState::Play() {
    if (m_playing) return;
    m_snapshot = SceneSerializer::Serialize(m_scene); // remember edit state
    m_selected = nullptr;
    ActionList::ResetVars();   // clear visual-script variables each Play session
    m_scene.Start();
    m_playing = true;
}

void EditorState::Stop() {
    if (!m_playing) return;
    m_playing = false;
    SceneSerializer::Deserialize(m_scene, m_snapshot); // restore edit state
    m_selected = nullptr;
}

void EditorState::Tick(float dt) {
    if (m_playing) { m_scene.Update(dt); NavigateUI(m_scene); }
}

} // namespace okay::editor
