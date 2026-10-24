// In-game build mode (BuildMode), logic links (LogicSource / LogicTarget) and
// creation save / load (BuildCreation). See docs/build_mode.md.
#include "okay/Components/BuildMode.hpp"
#include "okay/Components/LogicLinks.hpp"
#include "okay/Components/Buildable.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/Spawner.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/Crosshair.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/Cursor.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/DataPack.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace okay {

namespace {

// Objects build mode made for itself (ghost, wires, hotbar), or anything under one.
bool IsRuntime(const GameObject* g) {
    for (Transform* t = g ? g->transform : nullptr; t; t = t->Parent())
        if (t->gameObject && t->gameObject->tag == BuildMode::kRuntimeTag) return true;
    return false;
}

void Gather(GameObject* g, std::vector<GameObject*>& out) {
    if (!g || !g->transform) return;
    out.push_back(g);
    for (Transform* c : g->transform->Children()) if (c) Gather(c->gameObject, out);
}

template <class T>
std::vector<T*> InTree(GameObject* root) {
    std::vector<GameObject*> objs; Gather(root, objs);
    std::vector<T*> out;
    for (GameObject* g : objs) g->CollectComponents<T>(out);
    return out;
}

void SetTreeActive(GameObject* root, bool on) {
    std::vector<GameObject*> objs; Gather(root, objs);
    for (GameObject* g : objs) g->active = on;
}

GameObject* RootOf(GameObject* g) {
    if (!g || !g->transform) return g;
    Transform* t = g->transform;
    while (t->Parent()) t = t->Parent();
    return t->gameObject ? t->gameObject : g;
}

bool HasRigidbody(GameObject* g) {
    for (Transform* t = g ? g->transform : nullptr; t; t = t->Parent())
        if (t->gameObject && t->gameObject->GetComponent<Rigidbody3D>()) return true;
    return false;
}

float Axis(const Vec3& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }
void SetAxis(Vec3& v, int a, float f) { if (a == 0) v.x = f; else if (a == 1) v.y = f; else v.z = f; }

// The axis-aligned box around [mn, mx] after turning it by `q`.
void RotateBounds(const Vec3& mn, const Vec3& mx, const Quat& q, Vec3& omn, Vec3& omx) {
    const float inf = std::numeric_limits<float>::max();
    omn = {inf, inf, inf}; omx = {-inf, -inf, -inf};
    for (int i = 0; i < 8; ++i) {
        Vec3 c{(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z};
        Vec3 r = q * c;
        omn = Vec3::Min(omn, r); omx = Vec3::Max(omx, r);
    }
}

// The main camera's aim ray (centre of the screen, down its look axis) and the
// root object the camera belongs to (skipped by the ray: the player's own body).
bool CameraRay(const Scene& s, Vec3& origin, Vec3& dir, GameObject*& camOwner) {
    if (!s.mainCamera || !s.mainCamera->gameObject || !s.mainCamera->gameObject->transform) return false;
    Transform* cam = s.mainCamera->gameObject->transform;
    origin = cam->Position();
    dir = cam->Forward() * -1.0f;   // cameras look down -Z
    camOwner = RootOf(s.mainCamera->gameObject);
    return true;
}

bool ReadTextFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    std::string raw = ss.str();
    out = DataPack::Unpack(raw);   // decode if obfuscated (plaintext passes through)
    return !(out.empty() && DataPack::IsPacked(raw));
}

bool AnyBuildModeActive(const Scene& s) {
    for (BuildMode* m : s.FindObjectsOfType<BuildMode>()) if (m->Active()) return true;
    return false;
}

std::string KeyLabel(char c) {
    if (!c) return "";
    if (c == ' ') return "Space";
    if (c >= 'a' && c <= 'z') return std::string(1, (char)(c - 'a' + 'A'));
    return std::string(1, c);
}
std::string MouseLabel(int b) { return b == 0 ? "LMB" : b == 1 ? "RMB" : b == 2 ? "MMB" : ""; }

// A custom event to one object, the way the Send Event node delivers it: its
// graphs' On Custom Event nodes, its scripts' OnEvent_<name>(value) and its
// Actions lists' On Message.
void DeliverCustomEvent(GameObject* o, const std::string& name, const vs::VsValue& v, GameObject* sender) {
    for (auto* vc : o->GetComponents<VisualScriptComponent>()) vc->SendEvent(name, v, sender);
    for (auto* al : o->GetComponents<ActionList>()) {
        ActionList::Vars()["event_value"] = v.AsFloat();
        al->ReceiveMessage(name);
    }
    for (auto* sc : o->GetComponents<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEventWith("OnEvent_" + name, v);
}

} // namespace

// ===========================================================================
// LogicSource
// ===========================================================================

const char* LogicSource::KindName(Kind k) {
    switch (k) {
        case Kind::Button: return "Button";
        case Kind::PressurePlate: return "Pressure Plate";
        case Kind::TriggerZone: return "Trigger Zone";
        case Kind::Timer: return "Timer";
    }
    return "Button";
}

void LogicSource::Press() {
    if (!m_init) { m_init = true; m_out = startOn && toggle; }
    if (toggle) m_out = !m_out;
    else { m_out = true; m_timer = holdTime; }
}

void LogicSource::SetOutput(bool on) {
    m_init = true;
    m_out = on;
    if (kind == Kind::Button && !toggle && on) m_timer = holdTime;
}

bool LogicSource::HasLink(int id) const {
    return std::find(links.begin(), links.end(), id) != links.end();
}
bool LogicSource::AddLink(int id) {
    if (id == 0 || HasLink(id)) return false;
    links.push_back(id); m_cacheDirty = true;
    return true;
}
bool LogicSource::RemoveLink(int id) {
    auto it = std::find(links.begin(), links.end(), id);
    if (it == links.end()) return false;
    links.erase(it); m_cacheDirty = true;
    return true;
}

std::vector<LogicTarget*> LogicSource::Targets() const {
    Scene* s = GetScene();
    if (!s) return {};
    if (m_cacheDirty || m_cacheVersion != s->StructureVersion()) {
        m_cache.clear();
        for (int id : links)
            if (Buildable* b = Buildable::Find(*s, id))
                for (LogicTarget* t : InTree<LogicTarget>(b->gameObject)) m_cache.push_back(t);
        m_cacheVersion = s->StructureVersion();
        m_cacheDirty = false;
    }
    std::vector<LogicTarget*> out;
    for (LogicTarget* t : m_cache) {
        Buildable* b = Buildable::Of(t->gameObject);
        if (b && b->Removed()) continue;   // deleted this frame, destroyed at its end
        out.push_back(t);
    }
    return out;
}

bool LogicSource::AimedAt() const {
    Scene* s = GetScene();
    Vec3 origin, dir; GameObject* owner = nullptr;
    if (!s || !CameraRay(*s, origin, dir, owner)) return false;
    float gap = owner && owner->transform ? Vec3::Distance(origin, owner->transform->Position()) : 0.0f;
    for (const RaycastHit3D& h : s->physics3D().RaycastAll(*s, origin, dir, pressReach + gap)) {
        GameObject* g = h.gameObject;
        if (!g) return false;
        if (owner && g->IsSelfOrDescendantOf(owner)) continue;
        if (IsRuntime(g)) continue;
        const bool mine = g->IsSelfOrDescendantOf(gameObject);
        if (h.collider && h.collider->isTrigger && !mine) continue;   // aim through other zones
        return mine;
    }
    return false;
}

int LogicSource::CountOccupants() const {
    Scene* s = GetScene();
    if (!s || !transform) return 0;
    Vec3 sc = transform->LossyScale();
    Vec3 half = Vec3::Abs(Vec3::Scale(zoneSize, sc)) * 0.5f;
    // Shrink a little so the floor the zone rests on (or a wall it touches) doesn't count.
    half = Vec3::Max(half - Vec3{0.05f, 0.05f, 0.05f}, Vec3{0.01f, 0.01f, 0.01f});
    Vec3 c = transform->TransformPoint(zoneOffset);
    std::vector<GameObject*> seen;
    for (Collider3D* col : s->physics3D().OverlapBox(*s, c, half, transform->Rotation(),
                                                    Physics3D::kAllLayers, /*queryTriggers=*/false)) {
        GameObject* g = col ? col->gameObject : nullptr;
        if (!g || !g->active || g->IsSelfOrDescendantOf(gameObject) || IsRuntime(g)) continue;
        GameObject* root = RootOf(g);
        if (!activatorTag.empty()) {
            if (g->tag != activatorTag && root->tag != activatorTag) continue;
        } else {
            // Anything that moves: not static level geometry, and not placed
            // blocks unless they're physics props.
            if (g->isStatic || root->isStatic) continue;
            if (Buildable::Of(g) && !HasRigidbody(g)) continue;
        }
        if (std::find(seen.begin(), seen.end(), root) != seen.end()) continue;
        seen.push_back(root);
    }
    return (int)seen.size();
}

void LogicSource::Update(float dt) {
    if (!gameObject || !ScriptsRun(gameObject)) return;
    if (!m_init) {
        m_init = true;
        m_out = (kind == Kind::Button && toggle) ? startOn : false;
        m_timer = 0.0f;
        if (kind == Kind::Timer) m_timer = startOn ? 0.0f : std::min(onTime, std::max(interval, 0.05f));
    }
    switch (kind) {
        case Kind::Button: {
            Scene* s = GetScene();
            const bool key = pressKey && Input::GetKeyDown(pressKey);
            // The pad's X also turns pieces in build mode, so it only presses outside it.
            const bool pad = pressPadButton >= 0 && Input::GetGamepadButtonDown(pressPadButton) &&
                             !(s && AnyBuildModeActive(*s));
            if ((key || pad) && AimedAt()) Press();
            if (!toggle && m_out) { m_timer -= dt; if (m_timer <= 0.0f) m_out = false; }
            break;
        }
        case Kind::PressurePlate:
        case Kind::TriggerZone:
            m_occupants = CountOccupants();
            m_out = m_occupants > 0;
            break;
        case Kind::Timer: {
            const float cycle = std::max(interval, 0.05f);
            m_timer += dt;
            while (m_timer >= cycle) m_timer -= cycle;
            m_out = m_timer < onTime;
            break;
        }
    }
    for (LogicTarget* t : Targets()) t->Feed(m_out);
}

// ===========================================================================
// LogicTarget
// ===========================================================================

const char* LogicTarget::KindName(Kind k) {
    switch (k) {
        case Kind::Door: return "Door";
        case Kind::Mover: return "Mover";
        case Kind::Light: return "Light";
        case Kind::Spawner: return "Spawner";
        case Kind::Event: return "Event";
    }
    return "Door";
}

std::string LogicTarget::SignalName() const {
    if (!eventName.empty()) return eventName;
    return gameObject ? gameObject->name : std::string();
}

void LogicTarget::SetState(bool on) {
    m_init = true;
    if (on == m_state) return;
    m_state = on;
    Changed();
}

bool LogicTarget::ClosedPose(Vec3& pos, Quat& rot) const {
    if (!m_hasPose || m_t <= 0.0f) return false;
    pos = m_closedPos; rot = m_closedRot;
    return true;
}

void LogicTarget::ResetPose() {
    if (m_hasPose && m_t > 0.0f && transform) {
        transform->localPosition = m_closedPos;
        transform->localRotation = m_closedRot;
    }
    m_t = 0.0f; m_dir = 1;
}

void LogicTarget::ApplyLight() {
    if (kind != Kind::Light || !gameObject) return;
    for (Light* l : InTree<Light>(gameObject)) l->intensity = m_state ? lightIntensity : 0.0f;
    for (MeshRenderer* mr : InTree<MeshRenderer>(gameObject)) mr->emissive = m_state ? onEmissive : Color::Black;
}

void LogicTarget::Changed() {
    ApplyLight();
    if (Scene* s = GetScene())
        BroadcastLogicSignal(*s, gameObject, SignalName(), m_state,
                             kind == Kind::Event ? eventName : std::string());
}

GameObject* LogicTarget::SpawnNow() {
    Scene* s = GetScene();
    if (!s || !transform) return nullptr;
    if (spawnPrefab.empty()) {
        if (auto* sp = gameObject->GetComponent<okay::Spawner>()) return sp->SpawnOne();
        return nullptr;
    }
    m_spawned.erase(std::remove_if(m_spawned.begin(), m_spawned.end(), [](std::uint64_t id) {
        GameObject* g = GameObject::FromInstanceId(id);
        return !g || g->IsDestroyed();
    }), m_spawned.end());
    if (maxAlive > 0 && (int)m_spawned.size() >= maxAlive) return nullptr;
    GameObject* g = SceneSerializer::InstantiateFromFile(*s, spawnPrefab, nullptr);
    if (!g) return nullptr;
    if (g->transform) g->transform->SetPosition(transform->Position() + transform->Rotation() * spawnOffset);
    if (auto* b = g->GetComponent<Buildable>()) b->placed = false;   // spawns aren't part of the creation
    m_spawned.push_back(g->InstanceId());
    return g;
}

void LogicTarget::LateUpdate(float dt) {
    if (!gameObject || !ScriptsRun(gameObject)) { m_fed = false; return; }
    const bool powered = m_fed;
    m_fed = false;
    const bool act = powered != invert;
    if (!m_init) {
        m_init = true;
        m_prevAct = act;
        m_state = (mode == Mode::Follow) ? act : invert;
        m_powered = powered;
        ApplyLight();
    } else {
        bool next = m_state;
        const bool rising = act && !m_prevAct;
        if (mode == Mode::Follow) next = act;
        else if (rising) next = !m_state;
        m_prevAct = act;
        m_powered = powered;
        if (kind == Kind::Spawner && rising) SpawnNow();
        if (next != m_state) { m_state = next; Changed(); }
    }
    if ((kind != Kind::Door && kind != Kind::Mover) || !transform || m_suspended) return;
    // The closed pose follows the object while it's shut (so moving it in build
    // mode moves where it opens from); the open pose is relative to it.
    if (m_t <= 0.0f) { m_closedPos = transform->localPosition; m_closedRot = transform->localRotation; m_hasPose = true; }
    float target = 0.0f;
    if (kind == Kind::Door) target = m_state ? 1.0f : 0.0f;
    else if (m_state) target = m_dir > 0 ? 1.0f : 0.0f;
    const float prev = m_t;
    const float step = dt / std::max(moveTime, 0.01f);
    if (m_t < target) m_t = std::min(target, m_t + step);
    else if (m_t > target) m_t = std::max(target, m_t - step);
    if (kind == Kind::Mover && m_state) { if (m_t >= 1.0f) m_dir = -1; else if (m_t <= 0.0f) m_dir = 1; }
    if (m_hasPose && (m_t > 0.0f || prev > 0.0f)) {
        const float e = m_t * m_t * (3.0f - 2.0f * m_t);   // ease in / out
        transform->localPosition = m_closedPos + m_closedRot * (openOffset * e);
        transform->localRotation = m_closedRot * Quat::Euler(0.0f, openYaw * e, 0.0f);
    }
}

void BroadcastLogicSignal(Scene& scene, GameObject* who, const std::string& name, bool on,
                          const std::string& customEvent) {
    std::vector<GameObject*> all;   // collect first: a handler may spawn / destroy
    for (const auto& up : scene.Objects())
        if (up && up->active && !up->IsDestroyed()) all.push_back(up.get());
    for (GameObject* o : all) {
        if (!customEvent.empty()) DeliverCustomEvent(o, customEvent, vs::VsValue(on ? 1.0f : 0.0f), who);
        for (auto* vc : o->GetComponents<VisualScriptComponent>())
            vc->RaiseEvent("OnLogicSignal", who, name, vs::VsValue(on));
        for (auto* sc : o->GetComponents<ScriptComponent>()) {
            if (!sc->VM()) continue;
            sc->VM()->SetGlobal("signal_name", vs::VsValue(name));
            sc->VM()->SetGlobal("signal_on", vs::VsValue(on));
            sc->VM()->CallEventWith("on_logic_signal", vs::VsValue(name));
        }
    }
}

// ===========================================================================
// BuildCreation
// ===========================================================================

std::vector<GameObject*> BuildCreation::PlacedRoots(const Scene& scene) {
    std::vector<GameObject*> out;
    for (const auto& up : scene.Objects()) {
        GameObject* g = up.get();
        if (!g || g->IsDestroyed() || IsRuntime(g)) continue;
        auto* b = g->GetComponent<Buildable>();
        if (!b || !b->placed || b->Removed()) continue;
        Transform* p = g->transform ? g->transform->Parent() : nullptr;
        if (p && Buildable::Of(p->gameObject)) continue;   // part of a bigger placed object
        out.push_back(g);
    }
    return out;
}

std::string BuildCreation::SerializeObject(GameObject& root) {
    // Doors and movers are written shut, so a creation saved with a door open
    // doesn't load with the door's open pose as its closed one.
    struct Swap { Transform* t; Vec3 p; Quat r; };
    std::vector<Swap> swaps;
    for (LogicTarget* lt : InTree<LogicTarget>(&root)) {
        Vec3 p; Quat r;
        if (lt->transform && lt->ClosedPose(p, r)) {
            swaps.push_back({lt->transform, lt->transform->localPosition, lt->transform->localRotation});
            lt->transform->localPosition = p;
            lt->transform->localRotation = r;
        }
    }
    std::string text = SceneSerializer::SerializeObject(root);
    for (const Swap& s : swaps) { s.t->localPosition = s.p; s.t->localRotation = s.r; }
    return text;
}

std::string BuildCreation::Serialize(const Scene& scene) {
    std::vector<GameObject*> roots = PlacedRoots(scene);
    std::ostringstream out;
    out << "okaycreation " << kVersion << "\n";
    out << "objects " << roots.size() << "\n";
    for (GameObject* r : roots) {
        std::string text = SerializeObject(*r);
        if (!text.empty() && text.back() != '\n') text += '\n';
        out << "object\n" << text << "endobject\n";
    }
    return out.str();
}

bool BuildCreation::Save(const Scene& scene, const std::string& path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    // Write a temp file and rename it over the target, so a crash mid-write
    // never leaves a half-written creation.
    const std::string tmp = path + ".savetmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) { if (error) *error = "can't write " + path; return false; }
        f << Serialize(scene);
        f.flush();
        if (!f) { fs::remove(tmp, ec); if (error) *error = "can't write " + path; return false; }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        std::error_code ce;
        fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ce);
        fs::remove(tmp, ec);
        if (ce) { if (error) *error = "can't write " + path; return false; }
    }
    return true;
}

void BuildCreation::Clear(Scene& scene) {
    for (GameObject* r : PlacedRoots(scene)) {
        if (auto* b = r->GetComponent<Buildable>()) b->MarkRemoved();
        SetTreeActive(r, false);
        scene.Destroy(r);
    }
}

bool BuildCreation::Deserialize(Scene& scene, const std::string& text, bool replace,
                                std::string* error, std::vector<GameObject*>* added) {
    std::istringstream in(text);
    std::string line;
    auto fail = [&](const std::string& e) { if (error) *error = e; return false; };
    if (!std::getline(in, line)) return fail("empty creation file");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream hs(line);
    std::string magic; int version = 0;
    hs >> magic >> version;
    if (magic != "okaycreation") return fail("not a creation file");
    if (version > kVersion)
        return fail("this creation was made by a newer version (format " + std::to_string(version) + ")");

    // Split into objects; lines this version doesn't know are skipped.
    std::vector<std::string> blocks;
    std::string cur;
    bool inObject = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!inObject) {
            if (line == "object") { inObject = true; cur.clear(); }
        } else if (line == "endobject") {
            blocks.push_back(cur); inObject = false;
        } else {
            cur += line; cur += '\n';
        }
    }
    if (inObject) return fail("the creation file is cut short");

    // Check every object parses before touching the scene, so a bad file loads nothing.
    {
        Scene check("CreationCheck");
        for (const std::string& b : blocks) {
            std::string err;
            if (!SceneSerializer::InstantiateFromText(check, b, &err))
                return fail("bad object in creation: " + err);
        }
    }

    if (replace) Clear(scene);
    std::set<int> used;   // ids of objects already in the scene
    for (const auto& up : scene.Objects())
        if (up && !up->IsDestroyed())
            if (auto* b = up->GetComponent<Buildable>()) if (!b->Removed() && b->buildId) used.insert(b->buildId);
    int next = used.empty() ? 0 : *used.rbegin();

    std::vector<GameObject*> roots;
    for (const std::string& b : blocks)
        if (GameObject* g = SceneSerializer::InstantiateFromText(scene, b, nullptr)) roots.push_back(g);
    for (GameObject* g : roots)
        if (auto* b = g->GetComponent<Buildable>()) next = std::max(next, b->buildId);

    // Renumber ids that clash with the scene (or repeat), then fix links to them.
    std::map<int, int> remap;
    std::set<int> seen;
    for (GameObject* g : roots) {
        Buildable* b = g->GetComponent<Buildable>();
        if (!b) b = g->AddComponent<Buildable>();
        b->placed = true;
        if (b->buildId == 0 || used.count(b->buildId) || seen.count(b->buildId)) {
            const int fresh = ++next;
            if (b->buildId != 0 && !remap.count(b->buildId)) remap[b->buildId] = fresh;
            b->buildId = fresh;
        }
        seen.insert(b->buildId);
        if (!b->prefab.empty()) g->prefabSource = b->prefab;
    }
    if (!remap.empty())
        for (GameObject* g : roots)
            for (LogicSource* ls : InTree<LogicSource>(g))
                for (int& id : ls->links) { auto it = remap.find(id); if (it != remap.end()) id = it->second; }
    if (added) added->insert(added->end(), roots.begin(), roots.end());
    return true;
}

bool BuildCreation::Load(Scene& scene, const std::string& path, bool replace,
                         std::string* error, std::vector<GameObject*>* added) {
    std::string text;
    if (!ReadTextFile(path, text)) { if (error) *error = "can't open " + path; return false; }
    return Deserialize(scene, text, replace, error, added);
}

// ===========================================================================
// BuildMode
// ===========================================================================

struct BuildMode::Template {
    std::string prefab, text;
    std::unique_ptr<Scene> scene;   // a private scene holding one parsed copy (never updated)
    GameObject* root = nullptr;
    Vec3 mn{-0.5f, -0.5f, -0.5f}, mx{0.5f, 0.5f, 0.5f};
    Quat rotation = Quat::Identity;
    bool ok = false;
};

BuildMode::BuildMode() = default;
BuildMode::~BuildMode() = default;

BuildMode::Template* BuildMode::TemplateFor(const std::string& prefab) {
    if (prefab.empty()) return nullptr;
    auto it = m_templates.find(prefab);
    if (it != m_templates.end()) return it->second.get();
    auto t = std::make_unique<Template>();
    t->prefab = prefab;
    std::string text;
    if (ReadTextFile(prefab, text)) {
        t->scene = std::make_unique<Scene>("BuildTemplate");
        t->root = SceneSerializer::InstantiateFromText(*t->scene, text, nullptr);
        if (t->root && t->root->transform) {
            t->text = text;
            t->root->transform->localPosition = Vec3{0, 0, 0};
            t->rotation = t->root->transform->localRotation;
            LocalBounds(t->root, t->mn, t->mx);
            t->ok = true;
        }
    }
    Template* raw = t.get();
    m_templates[prefab] = std::move(t);
    return raw;
}

BuildMode::Template* BuildMode::SelectedTemplate() {
    if (m_slot < 0 || m_slot >= (int)palette.size()) return nullptr;
    Template* t = TemplateFor(palette[m_slot].prefab);
    return (t && t->ok) ? t : nullptr;
}

void BuildMode::ClearPalette() { palette.clear(); m_slot = 0; }

int BuildMode::AddToPalette(const std::string& name, const std::string& prefab, const std::string& icon) {
    palette.push_back({name, prefab, icon});
    return (int)palette.size() - 1;
}

void BuildMode::SelectSlot(int slot) {
    const int n = (int)palette.size();
    if (n <= 0) { m_slot = 0; return; }
    m_slot = ((slot % n) + n) % n;
}

void BuildMode::SetActive(bool on) {
    if (on == m_active) return;
    m_active = on;
    if (!on) {
        CancelCarry();
        m_linkPick = 0;
        m_linkMode = false;
        HideGhost();
        ClearHighlight();
        if (Scene* s = GetScene()) UpdateWires(*s, false, nullptr);
    }
}

void BuildMode::SetLinkMode(bool on) {
    if (on == m_linkMode) return;
    CancelCarry();
    m_linkMode = on;
    m_linkPick = 0;
    ClearHighlight();
}

BuildMode::Tool BuildMode::CurrentTool() const {
    if (Carried()) return Tool::Move;
    return m_linkMode ? Tool::Link : Tool::Place;
}

GameObject* BuildMode::Carried() const {
    GameObject* g = m_carry ? GameObject::FromInstanceId(m_carry) : nullptr;
    return (g && !g->IsDestroyed()) ? g : nullptr;
}

GameObject* BuildMode::LinkPick() const {
    GameObject* g = m_linkPick ? GameObject::FromInstanceId(m_linkPick) : nullptr;
    if (!g || g->IsDestroyed()) return nullptr;
    Buildable* b = Buildable::Of(g);
    return (b && b->Removed()) ? nullptr : g;
}

GameObject* BuildMode::Owner() const { return RootOf(gameObject); }

bool BuildMode::AimRay(Vec3& origin, Vec3& dir, float& reach) const {
    Scene* s = GetScene();
    GameObject* camOwner = nullptr;
    if (!s || !CameraRay(*s, origin, dir, camOwner)) return false;
    // Reach is measured from the player, so a third-person camera's distance doesn't eat into it.
    GameObject* o = Owner();
    float gap = (o && o->transform) ? Vec3::Distance(origin, o->transform->Position()) : 0.0f;
    reach = maxReach + gap;
    return true;
}

RaycastHit3D BuildMode::AimHit(Scene& s, const Vec3& origin, const Vec3& dir, float reach,
                               GameObject* ignore) const {
    GameObject* owner = Owner();
    for (const RaycastHit3D& h : s.physics3D().RaycastAll(s, origin, dir, reach)) {
        if (GameObject* g = h.gameObject) {
            if (owner && g->IsSelfOrDescendantOf(owner)) continue;
            if (ignore && g->IsSelfOrDescendantOf(ignore)) continue;
            if (IsRuntime(g)) continue;
            // Trigger volumes don't stop the aim, except build pieces (so zones can be picked).
            if (h.collider && h.collider->isTrigger && !Buildable::Of(g)) continue;
        }
        return h;
    }
    return RaycastHit3D{};
}

bool BuildMode::LocalBounds(const GameObject* root, Vec3& mn, Vec3& mx) {
    const float inf = std::numeric_limits<float>::max();
    Vec3 lo{inf, inf, inf}, hi{-inf, -inf, -inf};
    bool any = false;
    std::vector<GameObject*> objs; Gather(const_cast<GameObject*>(root), objs);
    for (GameObject* g : objs) {
        if (auto* mr = g->GetComponent<MeshRenderer>()) {
            if (!mr->mesh.vertices.empty()) {
                const Mat4 m = g->transform->LocalToWorldMatrix();
                for (const Vec3& v : mr->mesh.vertices) {
                    Vec3 p = m.MultiplyPoint(v);
                    lo = Vec3::Min(lo, p); hi = Vec3::Max(hi, p);
                }
                any = true;
            }
        }
        for (Collider3D* c : g->GetComponents<Collider3D>()) {
            Vec3 a, b; c->WorldAABB(a, b);
            lo = Vec3::Min(lo, a); hi = Vec3::Max(hi, b);
            any = true;
        }
    }
    if (!any || !root || !root->transform) { mn = {-0.5f, -0.5f, -0.5f}; mx = {0.5f, 0.5f, 0.5f}; return false; }
    const Vec3 o = root->transform->Position();
    mn = lo - o; mx = hi - o;
    return true;
}

Vec3 BuildMode::SnapToSurface(const Vec3& point, const Vec3& normal, const Vec3& mn, const Vec3& mx,
                              float grid, const Vec3& anchor) {
    // The surface's main axis: the piece sits flush against it along that axis.
    int k = 1;
    const float ax = std::fabs(normal.x), ay = std::fabs(normal.y), az = std::fabs(normal.z);
    if (ax > ay && ax > az) k = 0; else if (az > ay && az > ax) k = 2;
    Vec3 pos;
    for (int a = 0; a < 3; ++a) {
        const float p = Axis(point, a), lo = Axis(mn, a), hi = Axis(mx, a);
        if (a == k) {
            SetAxis(pos, a, Axis(normal, a) >= 0.0f ? p - lo : p - hi);
        } else {
            // Centre the piece on the aim point, then slide its low edge onto a grid line.
            const float half = (hi - lo) * 0.5f, centre = (hi + lo) * 0.5f;
            float edge = p - half;
            if (grid > 1e-4f) {
                const float o = Axis(anchor, a);
                edge = std::round((edge - o) / grid) * grid + o;
            }
            SetAxis(pos, a, edge + half - centre);
        }
    }
    return pos;
}

bool BuildMode::Blocked(Scene& s, const Vec3& pos, const Vec3& mn, const Vec3& mx, GameObject* ignore) const {
    const Vec3 c = pos + (mn + mx) * 0.5f;
    // Shrunk so pieces resting on (or beside) each other don't count as overlapping.
    Vec3 h = (mx - mn) * 0.5f - Vec3{0.04f, 0.04f, 0.04f};
    h = Vec3::Max(h, Vec3{0.01f, 0.01f, 0.01f});
    for (Collider3D* col : s.physics3D().OverlapBox(s, c, h, Quat::Identity, Physics3D::kAllLayers, false)) {
        GameObject* g = col ? col->gameObject : nullptr;
        if (!g) continue;
        if (ignore && g->IsSelfOrDescendantOf(ignore)) continue;
        if (IsRuntime(g)) continue;
        return true;
    }
    return false;
}

BuildPlacement BuildMode::Resolve(Scene& s, const Vec3& origin, const Vec3& dir, float reach,
                                  const Vec3& mn, const Vec3& mx, const Quat& yawRot, const Quat& finalRot,
                                  GameObject* ignore, bool counts) {
    BuildPlacement p;
    Vec3 rmn, rmx; RotateBounds(mn, mx, yawRot, rmn, rmx);
    RaycastHit3D h = AimHit(s, origin, dir, reach, ignore);
    Vec3 point = origin + dir * reach, normal = Vec3::Up;
    Vec3 anchor{0, 0, 0};
    if (h.hit) {
        p.hit = true; p.surface = h.gameObject;
        point = h.point; normal = h.normal;
        // Next to another build piece, line up with its edges instead of the world grid.
        if (Buildable* nb = Buildable::Of(h.gameObject)) {
            Vec3 bmn, bmx;
            if (nb->gameObject && LocalBounds(nb->gameObject, bmn, bmx))
                anchor = nb->gameObject->transform->Position() + bmn;
        }
    }
    p.position = SnapToSurface(point, normal, rmn, rmx, gridSize, anchor);
    p.rotation = finalRot;
    if (!h.hit && !allowAirPlacement) p.reason = "Aim at a surface";
    else if (h.hit && h.collider && !((buildLayers >> (h.collider->layer & 31)) & 1u)) p.reason = "Can't build here";
    else if (counts && maxObjects > 0 && PlacedCount() >= maxObjects) p.reason = "Limit reached (" + std::to_string(maxObjects) + ")";
    else if (Blocked(s, p.position, rmn, rmx, ignore)) p.reason = "Blocked";
    else p.valid = true;
    return p;
}

BuildPlacement BuildMode::ComputePlacement(Scene& scene, const Vec3& origin, const Vec3& dir, float reach) {
    Template* t = SelectedTemplate();
    if (!t) {
        BuildPlacement p;
        p.reason = palette.empty() ? "Palette is empty" : "Prefab not found";
        return p;
    }
    const Quat yawRot = Quat::Euler(0.0f, m_yaw, 0.0f);
    return Resolve(scene, origin, dir, Reach(reach), t->mn, t->mx, yawRot, yawRot * t->rotation, nullptr, true);
}

GameObject* BuildMode::PlaceAt(Scene& scene, const Vec3& origin, const Vec3& dir, float reach) {
    BuildPlacement p = ComputePlacement(scene, origin, dir, reach);
    m_last = p;
    if (!p.valid) return nullptr;
    return PlaceItem(m_slot, p.position, p.rotation);
}

GameObject* BuildMode::PickAt(Scene& scene, const Vec3& origin, const Vec3& dir, float reach) {
    RaycastHit3D h = AimHit(scene, origin, dir, Reach(reach), Carried());
    if (!h.hit || !h.gameObject) return nullptr;
    Buildable* b = Buildable::Of(h.gameObject);
    return (b && CanEdit(b->gameObject)) ? b->gameObject : nullptr;
}

bool BuildMode::RemoveAt(Scene& scene, const Vec3& origin, const Vec3& dir, float reach) {
    GameObject* g = PickAt(scene, origin, dir, reach);
    return g && Remove(g);
}

bool BuildMode::CanEdit(GameObject* go) const {
    Buildable* b = Buildable::Of(go);
    return b && b->editable && !b->Removed() && b->gameObject && !b->gameObject->IsDestroyed() &&
           b->gameObject->scene() == GetScene();
}

int BuildMode::NextId() {
    if (Scene* s = GetScene()) m_nextId = std::max(m_nextId, Buildable::MaxId(*s));
    return ++m_nextId;
}

Buildable* BuildMode::EnsureBuildable(GameObject* go) {
    if (!go) return nullptr;
    Buildable* b = go->GetComponent<Buildable>();
    if (!b) b = go->AddComponent<Buildable>();
    if (b->buildId == 0) b->buildId = NextId();
    return b;
}

GameObject* BuildMode::FindId(int id) const {
    Scene* s = GetScene();
    Buildable* b = s ? Buildable::Find(*s, id) : nullptr;
    return b ? b->gameObject : nullptr;
}

int BuildMode::PlacedCount() const {
    Scene* s = GetScene();
    return s ? (int)BuildCreation::PlacedRoots(*s).size() : 0;
}

std::string BuildMode::Snapshot(GameObject* go) {
    ClearHighlight();   // the selection outline isn't part of the object
    return BuildCreation::SerializeObject(*go);
}

GameObject* BuildMode::Restore(const std::string& data) {
    Scene* s = GetScene();
    if (!s) return nullptr;
    GameObject* g = SceneSerializer::InstantiateFromText(*s, data, nullptr);
    if (g) if (auto* b = g->GetComponent<Buildable>()) if (!b->prefab.empty()) g->prefabSource = b->prefab;
    return g;
}

void BuildMode::Destroy(GameObject* go, bool notify) {
    Scene* s = GetScene();
    if (!go || !s) return;
    if (m_hl == go->InstanceId()) ClearHighlight();
    if (m_carry == go->InstanceId()) m_carry = 0;
    // Marked removed first, so handlers of the removed event already see it gone
    // (build_count() / PlacedCount() no longer count it).
    if (auto* b = go->GetComponent<Buildable>()) b->MarkRemoved();
    if (notify) Notify(go, false);
    SetTreeActive(go, false);   // out of raycasts and logic right away
    s->Destroy(go);
}

void BuildMode::Push(Command c) {
    m_undo.push_back(std::move(c));
    if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void BuildMode::Notify(GameObject* go, bool placedEvt) {
    if (placedEvt && onPlaced) onPlaced(go);
    if (!placedEvt && onRemoved) onRemoved(go);
    Scene* s = GetScene();
    if (!s || !go) return;
    Buildable* b = go->GetComponent<Buildable>();
    const int id = b ? b->buildId : 0;
    const char* entry = placedEvt ? "OnObjectPlaced" : "OnObjectRemoved";
    const char* fn = placedEvt ? "on_object_placed" : "on_object_removed";
    std::vector<GameObject*> all;
    for (const auto& up : s->Objects())
        if (up && up->active && !up->IsDestroyed()) all.push_back(up.get());
    for (GameObject* o : all) {
        for (auto* vc : o->GetComponents<VisualScriptComponent>())
            vc->RaiseEvent(entry, go, go->name, vs::VsValue((float)id));
        for (auto* sc : o->GetComponents<ScriptComponent>()) {
            if (!sc->VM()) continue;
            sc->VM()->SetGlobal("build_object", vs::VsValue(go->name));
            sc->VM()->SetGlobal("build_id", vs::VsValue((float)id));
            sc->VM()->CallEventWith(fn, vs::VsValue(go->name));
        }
    }
}

GameObject* BuildMode::PlaceItem(int slot, const Vec3& position, const Quat& rotation) {
    Scene* s = GetScene();
    if (!s || slot < 0 || slot >= (int)palette.size()) return nullptr;
    if (maxObjects > 0 && PlacedCount() >= maxObjects) return nullptr;
    Template* t = TemplateFor(palette[slot].prefab);
    if (!t || !t->ok) return nullptr;
    GameObject* g = SceneSerializer::InstantiateFromText(*s, t->text, nullptr);
    if (!g || !g->transform) return nullptr;
    g->prefabSource = t->prefab;
    Buildable* b = g->GetComponent<Buildable>();
    if (!b) b = g->AddComponent<Buildable>();
    b->buildId = NextId();
    b->placed = true;
    b->prefab = t->prefab;
    for (LogicSource* ls : InTree<LogicSource>(g)) ls->links.clear();   // a fresh piece starts unwired
    g->transform->SetPosition(position);
    g->transform->SetRotation(rotation);
    Command c; c.type = Command::Type::Place; c.id = b->buildId; c.data = Snapshot(g);
    Push(c);
    Notify(g, true);
    return g;
}

bool BuildMode::Remove(GameObject* go) {
    if (!CanEdit(go)) return false;
    GameObject* root = Buildable::Of(go)->gameObject;
    if (root == Carried()) CancelCarry();
    Command c; c.type = Command::Type::Remove;
    c.id = EnsureBuildable(root)->buildId;
    c.data = Snapshot(root);
    Destroy(root, true);
    Push(c);
    return true;
}

bool BuildMode::SetPose(GameObject* go, const Vec3& position, const Quat& rotation) {
    if (!CanEdit(go)) return false;
    GameObject* root = Buildable::Of(go)->gameObject;
    for (LogicTarget* lt : InTree<LogicTarget>(root)) lt->ResetPose();
    Command c; c.type = Command::Type::Pose;
    c.id = EnsureBuildable(root)->buildId;
    c.p0 = root->transform->Position(); c.r0 = root->transform->Rotation();
    c.p1 = position; c.r1 = rotation;
    root->transform->SetPosition(position);
    root->transform->SetRotation(rotation);
    Push(c);
    return true;
}

bool BuildMode::BeginMove(GameObject* go) {
    if (Carried() || !CanEdit(go)) return false;
    GameObject* root = Buildable::Of(go)->gameObject;
    EnsureBuildable(root);
    ClearHighlight();
    for (LogicTarget* lt : InTree<LogicTarget>(root)) lt->SetSuspended(true);
    m_carry = root->InstanceId();
    m_carryNew = false;
    m_carryValid = true;
    m_carryPos0 = root->transform->Position();
    m_carryRot0 = root->transform->Rotation();
    m_carryYaw = 0.0f;
    LocalBounds(root, m_carryMn, m_carryMx);
    return true;
}

GameObject* BuildMode::Duplicate(GameObject* go) {
    if (Carried() || !CanEdit(go)) return nullptr;
    if (maxObjects > 0 && PlacedCount() >= maxObjects) return nullptr;
    GameObject* root = Buildable::Of(go)->gameObject;
    GameObject* copy = Restore(Snapshot(root));
    if (!copy) return nullptr;
    Buildable* b = copy->GetComponent<Buildable>();
    if (!b) b = copy->AddComponent<Buildable>();
    b->buildId = NextId();
    b->placed = true;
    for (LogicSource* ls : InTree<LogicSource>(copy)) ls->links.clear();
    for (LogicTarget* lt : InTree<LogicTarget>(copy)) lt->SetSuspended(true);
    m_carry = copy->InstanceId();
    m_carryNew = true;
    m_carryValid = false;
    m_carryPos0 = copy->transform->Position();
    m_carryRot0 = copy->transform->Rotation();
    m_carryYaw = 0.0f;
    LocalBounds(copy, m_carryMn, m_carryMx);
    return copy;
}

void BuildMode::UpdateCarry(Scene& scene, const Vec3& origin, const Vec3& dir, float reach) {
    GameObject* g = Carried();
    if (!g) return;
    const Quat yawRot = Quat::Euler(0.0f, m_carryYaw, 0.0f);
    BuildPlacement p = Resolve(scene, origin, dir, Reach(reach), m_carryMn, m_carryMx, yawRot,
                               yawRot * m_carryRot0, g, /*counts=*/false);
    g->transform->SetPosition(p.position);
    g->transform->SetRotation(p.rotation);
    m_carryValid = p.valid;
    m_last = p;
    Color c = p.valid ? validColor : invalidColor; c.a = 1.0f;
    SetHighlight(g, c);
}

bool BuildMode::DropCarried() {
    GameObject* g = Carried();
    if (!g || !m_carryValid) return false;
    ClearHighlight();
    for (LogicTarget* lt : InTree<LogicTarget>(g)) lt->SetSuspended(false);
    Buildable* b = EnsureBuildable(g);
    if (m_carryNew) {
        Command c; c.type = Command::Type::Place; c.id = b->buildId; c.data = Snapshot(g);
        Push(c);
        Notify(g, true);
    } else {
        Command c; c.type = Command::Type::Pose; c.id = b->buildId;
        c.p0 = m_carryPos0; c.r0 = m_carryRot0;
        c.p1 = g->transform->Position(); c.r1 = g->transform->Rotation();
        if (Vec3::Distance(c.p0, c.p1) > 1e-5f || Quat::Angle(c.r0, c.r1) > 1e-3f) Push(c);
    }
    m_carry = 0;
    return true;
}

void BuildMode::CancelCarry() {
    GameObject* g = Carried();
    m_carry = 0;
    if (!g) return;
    ClearHighlight();
    if (m_carryNew) { Destroy(g, false); return; }
    g->transform->SetPosition(m_carryPos0);
    g->transform->SetRotation(m_carryRot0);
    for (LogicTarget* lt : InTree<LogicTarget>(g)) lt->SetSuspended(false);
}

bool BuildMode::ToggleLinkIds(int src, int dst, bool on) {
    GameObject* s = FindId(src);
    if (!s) return false;
    auto sources = InTree<LogicSource>(s);
    if (sources.empty()) return false;
    return on ? sources[0]->AddLink(dst) : sources[0]->RemoveLink(dst);
}

bool BuildMode::Link(GameObject* source, GameObject* target) {
    if (!CanEdit(source) || !CanEdit(target)) return false;
    GameObject* sr = Buildable::Of(source)->gameObject;
    GameObject* tr = Buildable::Of(target)->gameObject;
    if (sr == tr) return false;
    auto sources = InTree<LogicSource>(sr);
    if (sources.empty() || InTree<LogicTarget>(tr).empty()) return false;
    const int sid = EnsureBuildable(sr)->buildId, tid = EnsureBuildable(tr)->buildId;
    if (!sources[0]->AddLink(tid)) return false;
    Command c; c.type = Command::Type::Link; c.id = sid; c.other = tid;
    Push(c);
    return true;
}

bool BuildMode::Unlink(GameObject* source, GameObject* target) {
    if (!CanEdit(source) || !CanEdit(target)) return false;
    GameObject* sr = Buildable::Of(source)->gameObject;
    GameObject* tr = Buildable::Of(target)->gameObject;
    auto sources = InTree<LogicSource>(sr);
    if (sources.empty()) return false;
    const int sid = EnsureBuildable(sr)->buildId, tid = EnsureBuildable(tr)->buildId;
    if (!sources[0]->RemoveLink(tid)) return false;
    Command c; c.type = Command::Type::Unlink; c.id = sid; c.other = tid;
    Push(c);
    return true;
}

bool BuildMode::LinkClickAt(Scene& scene, const Vec3& origin, const Vec3& dir, float reach) {
    GameObject* g = PickAt(scene, origin, dir, reach);
    GameObject* pick = LinkPick();
    if (!g) { m_linkPick = 0; return false; }
    const bool isSource = !InTree<LogicSource>(g).empty();
    const bool isTarget = !InTree<LogicTarget>(g).empty();
    if (pick && g != pick && isTarget) {
        auto sources = InTree<LogicSource>(pick);
        const int tid = EnsureBuildable(g)->buildId;
        if (!sources.empty() && sources[0]->HasLink(tid)) return Unlink(pick, g);
        return Link(pick, g);
    }
    if (isSource) { m_linkPick = (pick == g) ? 0 : g->InstanceId(); return true; }
    return false;
}

bool BuildMode::Undo() {
    CancelCarry();
    if (m_undo.empty()) return false;
    Command c = m_undo.back();
    m_undo.pop_back();
    switch (c.type) {
        case Command::Type::Place:
            if (GameObject* g = FindId(c.id)) Destroy(g, true);
            break;
        case Command::Type::Remove:
            if (GameObject* g = Restore(c.data)) Notify(g, true);
            break;
        case Command::Type::Pose:
            if (GameObject* g = FindId(c.id)) {
                for (LogicTarget* lt : InTree<LogicTarget>(g)) lt->ResetPose();
                g->transform->SetPosition(c.p0); g->transform->SetRotation(c.r0);
            }
            break;
        case Command::Type::Link:   ToggleLinkIds(c.id, c.other, false); break;
        case Command::Type::Unlink: ToggleLinkIds(c.id, c.other, true); break;
    }
    m_redo.push_back(c);
    return true;
}

bool BuildMode::Redo() {
    CancelCarry();
    if (m_redo.empty()) return false;
    Command c = m_redo.back();
    m_redo.pop_back();
    switch (c.type) {
        case Command::Type::Place:
            if (GameObject* g = Restore(c.data)) Notify(g, true);
            break;
        case Command::Type::Remove:
            if (GameObject* g = FindId(c.id)) Destroy(g, true);
            break;
        case Command::Type::Pose:
            if (GameObject* g = FindId(c.id)) {
                for (LogicTarget* lt : InTree<LogicTarget>(g)) lt->ResetPose();
                g->transform->SetPosition(c.p1); g->transform->SetRotation(c.r1);
            }
            break;
        case Command::Type::Link:   ToggleLinkIds(c.id, c.other, true); break;
        case Command::Type::Unlink: ToggleLinkIds(c.id, c.other, false); break;
    }
    m_undo.push_back(c);
    return true;
}

std::string BuildMode::CreationPath(const std::string& name, const std::string& folder) {
    std::string n = name.empty() ? std::string("creation") : name;
    const std::size_t slash = n.find_last_of("/\\");
    const bool hasDir = slash != std::string::npos;
    const std::string base = hasDir ? n.substr(slash + 1) : n;
    const bool hasExt = base.find('.') != std::string::npos;
    if (!hasExt) n += ".okaycreation";
    // A bare name goes in the creations folder; a folder or file name is used as given.
    if (!hasDir && !hasExt && !folder.empty()) n = folder + "/" + n;
    return Save::Resolve(n);
}

bool BuildMode::SaveCreation(const std::string& name, std::string* error) {
    Scene* s = GetScene();
    if (!s) { if (error) *error = "no scene"; return false; }
    CancelCarry();
    ClearHighlight();
    return BuildCreation::Save(*s, CreationPath(name, creationFolder), error);
}

bool BuildMode::LoadCreation(const std::string& name, std::string* error) {
    Scene* s = GetScene();
    if (!s) { if (error) *error = "no scene"; return false; }
    CancelCarry();
    ClearHighlight();
    m_linkPick = 0;
    if (!BuildCreation::Load(*s, CreationPath(name, creationFolder), true, error)) return false;
    ClearHistory();
    m_nextId = std::max(m_nextId, Buildable::MaxId(*s));
    return true;
}

void BuildMode::ClearCreation() {
    Scene* s = GetScene();
    if (!s) return;
    CancelCarry();
    ClearHighlight();
    m_linkPick = 0;
    BuildCreation::Clear(*s);
    ClearHistory();
}

// ---- Visuals ---------------------------------------------------------------

void BuildMode::SetHighlight(GameObject* go, const Color& c) {
    if (!go) { ClearHighlight(); return; }
    if (go->InstanceId() != m_hl) {
        ClearHighlight();
        m_hl = go->InstanceId();
        for (MeshRenderer* mr : InTree<MeshRenderer>(go))
            m_hlSaved.push_back({mr, mr->outline, mr->outlineColor, mr->outlineWidth, mr->emissive});
    }
    m_hlColor = c;
    // An outline where the renderer draws one, plus a soft glow that every renderer shows.
    for (SavedOutline& s : m_hlSaved) {
        s.mr->outline = true; s.mr->outlineColor = c; s.mr->outlineWidth = 0.04f;
        s.mr->emissive = {s.emissive.r + c.r * 0.35f, s.emissive.g + c.g * 0.35f, s.emissive.b + c.b * 0.35f, 1.0f};
    }
}

void BuildMode::ClearHighlight() {
    GameObject* g = m_hl ? GameObject::FromInstanceId(m_hl) : nullptr;
    if (g && !g->IsDestroyed())
        for (SavedOutline& s : m_hlSaved) {
            s.mr->outline = s.on; s.mr->outlineColor = s.color; s.mr->outlineWidth = s.width; s.mr->emissive = s.emissive;
        }
    m_hlSaved.clear();
    m_hl = 0;
}

void BuildMode::HideGhost() { if (m_ghost) SetTreeActive(m_ghost, false); }

void BuildMode::UpdateGhost(Scene& s, const BuildPlacement& p) {
    Template* t = SelectedTemplate();
    if (!showGhost || !t) { HideGhost(); return; }
    if (!m_ghost || m_ghostPrefab != t->prefab) {
        if (m_ghost) s.Destroy(m_ghost);
        // A see-through copy of the prefab's meshes (no colliders, scripts or logic).
        m_ghost = s.CreateGameObject("BuildGhost");
        m_ghost->tag = kRuntimeTag;
        m_ghostPrefab = t->prefab;
        for (const auto& up : t->scene->Objects()) {
            GameObject* o = up.get();
            auto* src = o ? o->GetComponent<MeshRenderer>() : nullptr;
            if (!src || !o->active) continue;
            GameObject* part = s.CreateGameObject("GhostPart");
            part->tag = kRuntimeTag;
            part->transform->SetParent(m_ghost->transform, false);
            part->transform->localPosition = o->transform->Position();
            part->transform->localRotation = o->transform->Rotation();
            part->transform->localScale = o->transform->LossyScale();
            auto* mr = part->AddComponent<MeshRenderer>();
            mr->mesh = src->mesh;
            mr->meshPath = src->meshPath;
            mr->unlit = true;
            mr->shader = MeshRenderer::Shader::Unlit;
            mr->groundShadow = false;
            mr->doubleSided = true;
        }
        m_ghostValid = !p.valid;   // force a colour update below
    }
    SetTreeActive(m_ghost, true);
    m_ghost->transform->SetPosition(p.position);
    m_ghost->transform->SetRotation(Quat::Euler(0.0f, m_yaw, 0.0f));
    if (m_ghostValid != p.valid) {
        m_ghostValid = p.valid;
        for (MeshRenderer* mr : InTree<MeshRenderer>(m_ghost)) mr->color = p.valid ? validColor : invalidColor;
    }
}

void BuildMode::UpdateWires(Scene& s, bool show, const Vec3* aimPoint) {
    struct Seg { Vec3 a, b; bool on; };
    std::vector<Seg> segs;
    auto anchorOf = [](GameObject* g) {
        Vec3 mn, mx;
        BuildMode::LocalBounds(g, mn, mx);
        return g->transform->Position() + (mn + mx) * 0.5f;
    };
    if (show) {
        for (LogicSource* ls : s.FindObjectsOfType<LogicSource>()) {
            if (!ls->gameObject || !ls->gameObject->active || IsRuntime(ls->gameObject)) continue;
            const Vec3 a = anchorOf(ls->gameObject);
            for (LogicTarget* t : ls->Targets())
                if (t->gameObject && t->gameObject->active) segs.push_back({a, anchorOf(t->gameObject), ls->Output()});
        }
        if (GameObject* pick = LinkPick())
            if (aimPoint) segs.push_back({anchorOf(pick), *aimPoint, false});
    }
    if (segs.empty() && m_wires.empty()) return;
    if (!m_wireRoot) {
        m_wireRoot = s.CreateGameObject("BuildWires");
        m_wireRoot->tag = kRuntimeTag;
    }
    // Two pooled objects per wire: a thin stretched box and a knob at the target end.
    while (m_wires.size() < segs.size() * 2) {
        GameObject* w = s.CreateGameObject("Wire");
        w->tag = kRuntimeTag;
        w->transform->SetParent(m_wireRoot->transform, false);
        auto* mr = w->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->unlit = true;
        mr->shader = MeshRenderer::Shader::Unlit;
        mr->groundShadow = false;
        m_wires.push_back(w);
    }
    for (std::size_t i = 0; i < m_wires.size(); ++i) {
        GameObject* w = m_wires[i];
        const std::size_t si = i / 2;
        if (si >= segs.size()) { w->active = false; continue; }
        const Seg& sg = segs[si];
        Vec3 d = sg.b - sg.a;
        const float len = d.Magnitude();
        w->active = len > 1e-3f;
        if (!w->active) continue;
        if (auto* mr = w->GetComponent<MeshRenderer>()) mr->color = sg.on ? wireOnColor : wireColor;
        if (i % 2 == 0) {
            w->transform->localPosition = (sg.a + sg.b) * 0.5f;
            w->transform->localRotation = Quat::FromToRotation(Vec3::Forward, d * (1.0f / len));
            w->transform->localScale = {0.06f, 0.06f, len};
        } else {
            w->transform->localPosition = sg.b;
            w->transform->localRotation = Quat::Identity;
            w->transform->localScale = {0.2f, 0.2f, 0.2f};
        }
    }
}

bool BuildMode::HotbarHovered() const {
    // A locked (mouse-look) cursor is hidden and its position is only a running
    // sum of motion, so it can't be pointing at the hotbar.
    if (!m_hud || !m_hud->active || Cursor::IsLocked()) return false;
    for (UIButton* b : m_hudSlots) if (b && b->IsHovered()) return true;
    return false;
}

void BuildMode::UpdateHud(Scene& s) {
    const bool show = m_active && showHotbar;
    if (!m_hud && !show) return;
    if (!m_hud) {
        m_hud = s.CreateGameObject("BuildModeHUD");
        m_hud->tag = kRuntimeTag;
        auto* cv = m_hud->AddComponent<Canvas>();
        cv->sortOrder = 40;
        auto mk = [&](const char* name) {
            GameObject* g = s.CreateGameObject(name);
            g->tag = kRuntimeTag;
            g->transform->SetParent(m_hud->transform, false);
            return g;
        };
        m_hudPanel = mk("HotbarPanel")->AddComponent<UIPanel>();
        m_hudPanel->anchor = UIAnchor::BottomCenter;
        m_hudPanel->color = Color::FromBytes(20, 23, 31, 200);
        m_hudPanel->cornerRadius = 12.0f;
        auto text = [&](const char* name, float px, const Color& c) {
            auto* tr = mk(name)->AddComponent<TextRenderer>();
            tr->screenSpace = true;
            tr->anchor = UIAnchor::BottomCenter;
            tr->align = 1;
            tr->pixelSize = px;
            tr->color = c;
            tr->size = {900.0f, 24.0f};
            tr->text = "";
            return tr;
        };
        // An aim reticle, unless the game already draws one.
        if (!s.FindObjectOfType<Crosshair>()) mk("BuildCrosshair")->AddComponent<Crosshair>()->dot = true;
        m_hudTitle = text("HotbarTitle", 2.0f, Color::White);
        m_hudHint = text("HotbarHint", 1.5f, Color::FromBytes(190, 198, 214));
        m_hudSig = "\x01";
    }
    // Rebuild the slot buttons when the palette changes.
    std::string sig;
    for (const auto& it : palette) sig += it.name + '\x1f' + it.icon + '\x1e';
    if (sig != m_hudSig) {
        for (UIButton* b : m_hudSlots) if (b && b->gameObject) s.Destroy(b->gameObject);
        m_hudSlots.clear();
        for (std::size_t i = 0; i < palette.size(); ++i) {
            GameObject* g = s.CreateGameObject("HotbarSlot");
            g->tag = kRuntimeTag;
            g->transform->SetParent(m_hud->transform, false);
            auto* b = g->AddComponent<UIButton>();
            b->anchor = UIAnchor::BottomCenter;
            b->size = {64.0f, 64.0f};
            b->cornerRadius = 10.0f;
            b->fontScale = 1.25f;
            b->focusable = false;   // keys pick slots; no menu-navigation focus ring
            const auto& it = palette[i];
            if (!it.icon.empty()) { b->icon = it.icon; b->iconSize = 44.0f; b->label = ""; }
            else b->label = it.name.substr(0, 7);
            m_hudSlots.push_back(b);
        }
        m_hudSig = sig;
    }
    SetTreeActive(m_hud, show);
    if (!show) return;

    // Layout: a row of slots docked to the bottom centre, text above it.
    const float slot = 64.0f, gap = 8.0f;
    const int n = (int)m_hudSlots.size();
    const float rowW = n > 0 ? n * slot + (n - 1) * gap : 240.0f;
    m_hudPanel->size = {rowW + 20.0f, slot + 20.0f};
    m_hudPanel->position = {0.0f, -14.0f};
    for (int i = 0; i < n; ++i) {
        UIButton* b = m_hudSlots[i];
        b->position = {i * (slot + gap) + slot * 0.5f - rowW * 0.5f, -24.0f};
        const bool sel = i == m_slot;
        b->color = sel ? Color::FromBytes(66, 150, 250) : Color::FromBytes(40, 45, 58, 235);
        // No hover tint under mouse-look: the hidden cursor isn't really over a slot.
        b->hoverColor = Cursor::IsLocked() ? b->color
                      : sel ? Color::FromBytes(92, 166, 252) : Color::FromBytes(58, 64, 80, 240);
        b->borderWidth = sel ? 2.0f : 1.0f;
        b->borderColor = sel ? Color::White : Color::FromBytes(255, 255, 255, 40);
        if (b->WasClicked() && !Cursor::IsLocked()) SelectSlot(i);
    }
    m_hudTitle->screenPos = {0.0f, -106.0f};
    m_hudHint->screenPos = {0.0f, -132.0f};

    const BuildBindings& k = bindings;
    auto key = [](const std::string& label, const char* what) {
        return label.empty() ? std::string() : label + " " + what + "   ";
    };
    std::string title, hint;
    if (GameObject* c = Carried()) {
        title = std::string(m_carryNew ? "COPY  " : "MOVE  ") + c->name;
        if (!m_carryValid && !m_last.reason.empty()) title += "  -  " + m_last.reason;
        hint = key(MouseLabel(k.place), "drop") + key(KeyLabel(k.rotate), "rotate") + key(MouseLabel(k.remove), "cancel");
    } else if (m_linkMode) {
        GameObject* pick = LinkPick();
        title = pick ? "LINK  " + pick->name + "  -  now click a target" : std::string("LINK  -  click a button, plate, zone or timer");
        hint = key(MouseLabel(k.place), "pick / link") + key(MouseLabel(k.remove), "clear") + key(KeyLabel(k.link), "done");
    } else {
        title = palette.empty() ? std::string("BUILD  -  empty palette")
                                : "[" + std::to_string(m_slot + 1) + "] " + palette[m_slot].name;
        if (!palette.empty() && !m_last.valid && !m_last.reason.empty()) title += "  -  " + m_last.reason;
        std::string del = MouseLabel(k.remove);
        if (k.removeKey) del += (del.empty() ? "" : "/") + KeyLabel(k.removeKey);
        hint = key(MouseLabel(k.place), "place") + key(del, "delete") + key(KeyLabel(k.rotate), "rotate") +
               key(KeyLabel(k.move), "move") + key(KeyLabel(k.duplicate), "copy") + key(KeyLabel(k.link), "link") +
               key(KeyLabel(k.undo), "undo") + key(KeyLabel(k.redo), "redo") + key(KeyLabel(k.toggle), "exit");
    }
    m_hudTitle->text = title;
    m_hudHint->text = hint;
}

void BuildMode::DestroyRuntime() {
    Scene* s = GetScene();
    ClearHighlight();
    if (s) {
        if (m_ghost) s->Destroy(m_ghost);
        if (m_wireRoot) s->Destroy(m_wireRoot);
        if (m_hud) s->Destroy(m_hud);
    }
    m_ghost = nullptr; m_ghostPrefab.clear();
    m_wireRoot = nullptr; m_wires.clear();
    m_hud = nullptr; m_hudPanel = nullptr; m_hudSlots.clear(); m_hudTitle = m_hudHint = nullptr; m_hudSig.clear();
}

void BuildMode::OnDestroy() { CancelCarry(); DestroyRuntime(); }

void BuildMode::OnDisable() { SetActive(false); if (Scene* s = GetScene()) UpdateHud(*s); }

void BuildMode::OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) {
    auto dies = [&](const GameObject* g) { return g && dying.count(g) != 0; };
    if (dies(m_ghost)) { m_ghost = nullptr; m_ghostPrefab.clear(); }
    if (dies(m_wireRoot)) { m_wireRoot = nullptr; m_wires.clear(); }
    m_wires.erase(std::remove_if(m_wires.begin(), m_wires.end(), dies), m_wires.end());
    if (dies(m_hud)) { m_hud = nullptr; m_hudPanel = nullptr; m_hudSlots.clear(); m_hudTitle = m_hudHint = nullptr; m_hudSig.clear(); }
    m_hudSlots.erase(std::remove_if(m_hudSlots.begin(), m_hudSlots.end(),
                                    [&](UIButton* b) { return !b || dies(b->gameObject); }), m_hudSlots.end());
    if (m_hl) for (const GameObject* g : dying) if (g->InstanceId() == m_hl) { m_hlSaved.clear(); m_hl = 0; break; }
}

// ---- Per-frame input --------------------------------------------------------

void BuildMode::Update(float /*dt*/) {
    if (!gameObject || !ScriptsRun(gameObject)) return;
    Scene* s = GetScene();
    if (!s) return;
    if (!m_started) { m_started = true; if (startActive) SetActive(true); }
    const BuildBindings& k = bindings;
    auto keyDown = [](char c) { return c != 0 && Input::GetKeyDown(c); };
    auto padDown = [](int b) { return b >= 0 && Input::GetGamepadButtonDown(b); };
    auto mouseDown = [](int b) { return b >= 0 && Input::GetMouseButtonDown(b); };

    if (keyDown(k.toggle) || padDown(k.padToggle)) SetActive(!m_active);
    UpdateHud(*s);
    if (!m_active) return;

    Vec3 origin, dir; float reach = maxReach;
    if (!AimRay(origin, dir, reach)) { HideGhost(); return; }

    // Palette slot.
    const int n = (int)palette.size();
    if (n > 0) {
        if (k.numberKeys)
            for (int i = 0; i < 9 && i < n; ++i) if (Input::GetKeyDown((char)('1' + i))) SelectSlot(i);
        if (k.mouseWheel) {
            const float w = Input::MouseWheel();
            if (w > 0.0f) SelectSlot(m_slot - 1); else if (w < 0.0f) SelectSlot(m_slot + 1);
        }
        if (keyDown(k.nextItem) || padDown(k.padNext)) SelectSlot(m_slot + 1);
        if (keyDown(k.prevItem) || padDown(k.padPrev)) SelectSlot(m_slot - 1);
    }
    if (keyDown(k.undo) || padDown(k.padUndo)) Undo();
    if (keyDown(k.redo) || padDown(k.padRedo)) Redo();
    if (keyDown(k.link) || padDown(k.padLink)) SetLinkMode(!m_linkMode);

    bool place = mouseDown(k.place) || padDown(k.padPlace);
    if (place && HotbarHovered()) place = false;   // clicking a slot isn't a placement
    const bool remove = mouseDown(k.remove) || keyDown(k.removeKey) || padDown(k.padRemove);
    const bool rotate = keyDown(k.rotate) || padDown(k.padRotate);
    const float step = Input::GetKey(Input::KeyShift) ? -rotationStep : rotationStep;

    if (Carried()) {
        HideGhost();
        UpdateWires(*s, false, nullptr);
        if (rotate) m_carryYaw = std::fmod(m_carryYaw + step, 360.0f);
        UpdateCarry(*s, origin, dir, reach);
        if (place || keyDown(k.move) || padDown(k.padMove)) DropCarried();
        else if (remove) CancelCarry();
        return;
    }

    if (m_linkMode) {
        HideGhost();
        GameObject* hover = PickAt(*s, origin, dir, reach);
        if (hover && InTree<LogicSource>(hover).empty() && InTree<LogicTarget>(hover).empty()) hover = nullptr;
        if (place) LinkClickAt(*s, origin, dir, reach);
        else if (remove) m_linkPick = 0;
        GameObject* pick = LinkPick();
        if (hover && hover != pick) SetHighlight(hover, highlightColor);
        else if (pick) SetHighlight(pick, wireOnColor);
        else ClearHighlight();
        RaycastHit3D h = AimHit(*s, origin, dir, reach, nullptr);
        const Vec3 aim = h.hit ? h.point : origin + dir * reach;
        UpdateWires(*s, true, &aim);
        return;
    }

    UpdateWires(*s, false, nullptr);
    if (rotate) m_yaw = std::fmod(m_yaw + step, 360.0f);
    GameObject* hover = PickAt(*s, origin, dir, reach);
    m_last = ComputePlacement(*s, origin, dir, reach);
    UpdateGhost(*s, m_last);
    if (place) {
        if (m_last.valid) PlaceItem(m_slot, m_last.position, m_last.rotation);
    } else if (remove) {
        if (hover) Remove(hover);
        hover = nullptr;
    } else if (keyDown(k.move) || padDown(k.padMove)) {
        if (hover && BeginMove(hover)) { HideGhost(); return; }
    } else if (keyDown(k.duplicate) || padDown(k.padDuplicate)) {
        if (hover && Duplicate(hover)) { HideGhost(); return; }
    }
    SetHighlight(hover, highlightColor);
}

} // namespace okay
