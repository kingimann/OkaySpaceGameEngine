#include "okay/Net/Matchmaker.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "EditorState.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/ColliderFit.hpp"
#include <filesystem>

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
    // Online services are part of the engine: a Steam simulation backend by
    // default, the real Steamworks backend when built with -DOKAY_WITH_STEAM.
    m_steam = CreateSteamService();
    SteamConfig sc; sc.appId = 480; // Spacewar test app id
    if (m_steam) m_steam->Initialize(sc);
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
    if (go) { m_scene.Destroy(go); m_scene.FlushDestroyed(); }
    m_net = nullptr;
}

void EditorState::TickServices(float dt) {
    if (m_steam) m_steam->RunCallbacks();
    // While playing, the scene's own Update() already ticks m_net (it's a scene
    // component); ticking it again here would double-step the netcode. Only drive
    // it from here in edit mode (so Host/Join previews work without pressing Play).
    if (m_net && !m_playing) m_net->Update(dt);
}

void EditorState::Achievement(const std::string& id) {
    if (m_steam) m_steam->UnlockAchievement(id);
}

// Undo/redo rebuild every object, so the selection is carried across by position
// in the scene's object list (names double-check it) — the Inspector and the
// Animation tab stay on what you were editing instead of going blank.
void EditorState::RestoreKeepingSelection(const std::string& snapshot) {
    if (finishFrozenTick) finishFrozenTick();   // never rebuild a scene frozen mid-frame
    std::vector<std::pair<int, std::string>> keep;
    auto indexOf = [&](GameObject* g) {
        int i = 0;
        for (const auto& up : m_scene.Objects()) { if (up.get() == g) return i; ++i; }
        return -1;
    };
    for (GameObject* g : m_multi) if (g) keep.push_back({indexOf(g), g->name});
    int primary = m_selected ? indexOf(m_selected) : -1;
    SceneSerializer::Deserialize(m_scene, snapshot);
    Select(nullptr);   // Deserialize freed every object; clear m_multi too (no dangling ptrs)
    const auto& objs = m_scene.Objects();
    GameObject* prim = nullptr;
    for (const auto& k : keep) {
        GameObject* g = nullptr;
        if (k.first >= 0 && k.first < (int)objs.size() && objs[k.first]->name == k.second) g = objs[k.first].get();
        if (!g) continue;
        if (m_multi.empty()) Select(g); else ToggleSelect(g);
        if (k.first == primary) prim = g;
    }
    if (prim) m_selected = prim;
    dirty = true;
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
    RestoreKeepingSelection(s);
    return true;
}

bool EditorState::Redo() {
    if (m_redo.empty()) return false;
    m_undo.push_back(SceneSerializer::Serialize(m_scene));
    std::string s = m_redo.back(); m_redo.pop_back();
    StopNetwork();
    RestoreKeepingSelection(s);
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

GameObject* EditorState::CreateCube(const std::string& name) {
    PushUndo();
    GameObject* go = m_scene.CreateGameObject(name);
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube();
    mr->color = Color::FromBytes(200, 200, 205); // neutral gray
    AddFittedBoxCollider(go);
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
        GameObject* clone = m_scene.Instantiate(*g);
        if (clone) { clone->transform->localPosition += Vec3{0.5f, 0.5f, 0.0f}; clones.push_back(clone); }
    }
    if (clones.empty()) return nullptr;
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
    PushUndo();
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

void EditorState::NewScene() {
    if (finishFrozenTick) finishFrozenTick();   // never rebuild a scene frozen mid-frame
    PushUndo();
    if (m_playing) Stop();
    StopNetwork();
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
    dirty = false;
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
    dirty = false;
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
        ground->AddComponent<MeshCollider3D>();         // things rest on it
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

void EditorState::NewPlatformer3D() {
    NewScene();
    m_suppressUndo = true;
    Templates::Platformer3D(m_scene);
    m_suppressUndo = false;
    view3D = true;
    camTarget = {0, 1, 0};
    camDist = 14.0f;
    m_selected = m_scene.Find("Player");
    dirty = false;
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
    dirty = false;
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
    dirty = false;
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
    dirty = false;
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
    dirty = false;
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
    dirty = false;
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
    dirty = false;
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
    dirty = false;
}

void EditorState::NewMultiplayer() {
    NewScene();
    m_suppressUndo = true;
    Templates::Multiplayer(m_scene);
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

void EditorState::NewTopDownShooter() {
    NewScene();
    m_suppressUndo = true;
    Templates::TopDownShooter(m_scene);
    m_suppressUndo = false;
    view3D = true;
    m_selected = m_scene.Find("Player");
    dirty = false;
}

void EditorState::NewPlatformer25D() {
    NewScene();
    m_suppressUndo = true;
    Templates::Platformer25D(m_scene);
    m_suppressUndo = false;
    view3D = true;
    m_selected = m_scene.Find("Player");
    dirty = false;
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
    dirty = false;
}

void EditorState::NewVehicle2D() {
    NewScene();
    m_suppressUndo = true;
    Templates::Vehicle2D(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Car");
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

void EditorState::NewBrickBreaker() {
    NewScene();
    m_suppressUndo = true;
    Templates::BrickBreaker(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Paddle");
    dirty = false;
}

void EditorState::NewInventory() {
    NewScene();
    m_suppressUndo = true;
    Templates::Inventory(m_scene);
    m_suppressUndo = false;
    view3D = false;
    m_selected = m_scene.Find("Bag");
    dirty = false;
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

bool EditorState::Save(const std::string& path) {
    SaveEditedMeshAssets();
    if (!SceneSerializer::SaveToFile(m_scene, path)) return false;
    m_path = path;
    dirty = false;
    return true;
}

bool EditorState::Load(const std::string& path, std::string* error) {
    if (finishFrozenTick) finishFrozenTick();   // never rebuild a scene frozen mid-frame
    if (m_playing) Stop();
    PushUndo();
    StopNetwork();
    if (!SceneSerializer::LoadFromFile(m_scene, path, error)) return false;
    m_path = path;
    // Re-home the project to the scene's own project folder (the parent of its
    // Assets/ dir), so opening another project's scene switches the Project panel
    // and asset paths over instead of keeping the previous project's files around.
    {
        namespace fs = std::filesystem;
        for (fs::path p = fs::absolute(fs::path(path)).parent_path(); !p.empty(); p = p.parent_path()) {
            if (p.filename() == "Assets") { m_projectDir = p.parent_path().string(); break; }
            if (p == p.parent_path()) break;   // reached the filesystem root
        }
    }
    Select(nullptr);   // load rebuilt the scene; clear m_multi too (no dangling ptrs)
    dirty = false;
    return true;
}

void EditorState::Play() {
    if (m_playing) return;
    m_snapshot = SceneSerializer::Serialize(m_scene); // remember edit state
    m_scene.UseFixedStepPhysics(true);   // physics as the game runs it: fixed steps, drawn smoothly
    Character::SetAssetBaseDir(m_projectDir);   // a Character's project-relative clips file
    Select(nullptr);   // Start()/reset may rebuild; clear m_multi too (no dangling ptrs)
    ActionList::ResetVars();   // clear visual-script variables each Play session
    vs::ForgetSceneVariables(&m_scene);   // scene / global graph variables start empty too
    vs::ClearGlobalVariables();
    Game::Reset();             // clear stale pause/quit state from a prior session
    // Audio files named on AudioSources play in Play mode without pressing Load.
    {
        std::vector<std::string> missing;
        LoadSceneAudio(m_scene, m_projectDir, AudioMixer::sampleRate, &missing);
        for (const auto& p : missing) Log::Warning("Audio Source: can't read ", p, " (WAV, OGG or MP3)");
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
    m_playing = false;
    m_scene.UseFixedStepPhysics(false);
    Game::Reset();   // unpause + clear quit so the next Play starts clean
    ActionList::DebugPaused() = false; ActionList::StepBudget() = 0;   // never leave Actions frozen after Stop
    // Deserialize rebuilds the scene, destroying every live component — including
    // any NetworkManager m_net points at. Drop the pointer first so TickServices
    // never dereferences freed memory (a use-after-free crash).
    m_net = nullptr;
    Matchmaker::Shared().Cancel();   // a search started in play mode ends with it
    m_scene.runScripts = false;
    SceneSerializer::Deserialize(m_scene, m_snapshot); // restore edit state
    Select(nullptr);   // Deserialize freed every object; clear m_multi too (no dangling ptrs)
}

void EditorState::Tick(float dt) {
    m_scene.runScripts = m_playing;   // edit-mode flushes (UI tools, previews) never run game code
    if (m_playing) {
        m_scene.Update(dt); NavigateUI(m_scene);
        Matchmaker::Shared().Pump(m_scene.FindObjectOfType<NetworkManager>());   // net_queue_join(): connect a found match
    }
}

} // namespace okay::editor
