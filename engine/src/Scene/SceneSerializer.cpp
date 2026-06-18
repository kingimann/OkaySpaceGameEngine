#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Collider2D.hpp"

#include <functional>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
// Quote a string for the line-based format (escapes quotes/backslashes).
std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// Read a quoted token from a stream (assumes leading quote already trimmed by >>).
std::string ReadQuoted(std::istream& in) {
    std::string tok;
    in >> std::ws;
    if (in.peek() != '"') { in >> tok; return tok; }
    in.get(); // opening quote
    std::string out;
    char c;
    while (in.get(c)) {
        if (c == '\\') { if (in.get(c)) out += c; }
        else if (c == '"') break;
        else out += c;
    }
    return out;
}

int IndexOf(const Scene& scene, const GameObject* go) {
    const auto& objs = scene.Objects();
    for (std::size_t i = 0; i < objs.size(); ++i)
        if (objs[i].get() == go) return static_cast<int>(i);
    return -1;
}

// Write a GameObject's Transform + all known components (no header/parent line).
void WriteComponents(std::ostream& out, GameObject* go) {
    Transform* t = go->transform;
    const Vec3& p = t->localPosition;
    const Quat& q = t->localRotation;
    const Vec3& s = t->localScale;
    out << "  transform " << p.x << " " << p.y << " " << p.z << " "
        << q.x << " " << q.y << " " << q.z << " " << q.w << " "
        << s.x << " " << s.y << " " << s.z << "\n";
    if (auto* sr = go->GetComponent<SpriteRenderer>()) {
        out << "  sprite " << static_cast<int>(sr->glyph) << " "
            << sr->color.r << " " << sr->color.g << " " << sr->color.b << " "
            << sr->color.a << " " << sr->size.x << " " << sr->size.y << "\n";
    }
    if (auto* cam = go->GetComponent<Camera>()) {
        out << "  camera " << (int)cam->projection << " " << cam->orthographicSize << " "
            << cam->fieldOfView << " "
            << cam->backgroundColor.r << " " << cam->backgroundColor.g << " "
            << cam->backgroundColor.b << " " << cam->backgroundColor.a << " "
            << (cam->main ? 1 : 0) << "\n";
    }
    if (auto* mr = go->GetComponent<MeshRenderer>()) {
        out << "  mesh " << Quote(mr->mesh.name.empty() ? "Cube" : mr->mesh.name) << " "
            << mr->color.r << " " << mr->color.g << " " << mr->color.b << " "
            << mr->color.a << " " << (mr->wireframe ? 1 : 0) << "\n";
    }
    if (auto* rb = go->GetComponent<Rigidbody2D>()) {
        out << "  rigidbody2d " << (int)rb->bodyType << " " << rb->gravityScale << " "
            << rb->mass << " " << rb->drag << " " << rb->bounciness << "\n";
    }
    if (auto* bc = go->GetComponent<BoxCollider2D>()) {
        out << "  boxcollider2d " << bc->size.x << " " << bc->size.y << " "
            << bc->offset.x << " " << bc->offset.y << " " << (bc->isTrigger ? 1 : 0) << "\n";
    }
    if (auto* cc = go->GetComponent<CircleCollider2D>()) {
        out << "  circlecollider2d " << cc->radius << " "
            << cc->offset.x << " " << cc->offset.y << " " << (cc->isTrigger ? 1 : 0) << "\n";
    }
    if (auto* sc = go->GetComponent<ScriptComponent>()) {
        out << "  script " << Quote(sc->Language()) << " " << Quote(sc->Source()) << "\n";
        if (!sc->Path().empty()) out << "  scriptpath " << Quote(sc->Path()) << "\n";
    }
    if (auto* vsc = go->GetComponent<VisualScriptComponent>()) {
        out << "  visualscript " << Quote(vsc->Source()) << "\n";
    }
}
} // namespace

std::string SceneSerializer::Serialize(const Scene& scene) {
    std::ostringstream out;
    out << "okayscene 1\n";
    out << "name " << Quote(scene.Name()) << "\n";
    const auto& objs = scene.Objects();
    for (std::size_t i = 0; i < objs.size(); ++i) {
        GameObject* go = objs[i].get();
        Transform* t = go->transform;
        out << "gameobject " << i << " " << Quote(go->name) << "\n";
        out << "  active " << (go->active ? 1 : 0) << "\n";
        if (!go->tag.empty()) out << "  tag " << Quote(go->tag) << "\n";
        int parent = t->Parent() ? IndexOf(scene, t->Parent()->gameObject) : -1;
        out << "  parent " << parent << "\n";
        WriteComponents(out, go);
        out << "end\n";
    }
    return out.str();
}

bool SceneSerializer::SaveToFile(const Scene& scene, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << Serialize(scene);
    return static_cast<bool>(f);
}

// Parse a document into `scene`. If `clear` is false, objects are appended
// (used by Instantiate); `firstNew` receives the first created GameObject.
static bool ParseInto(Scene& scene, const std::string& text, bool clear,
                      GameObject** firstNew, std::string* error) {
    if (clear) scene.Clear();
    std::istringstream in(text);
    std::string token;

    if (!(in >> token) || token != "okayscene") {
        if (error) *error = "missing 'okayscene' header";
        return false;
    }
    int version = 0; in >> version;

    // file index -> created GameObject, plus deferred parent links.
    std::unordered_map<int, GameObject*> byIndex;
    std::vector<std::pair<int, int>> parentLinks; // child index -> parent index

    while (in >> token) {
        if (token == "name") {
            scene.SetName(ReadQuoted(in));
        } else if (token == "gameobject") {
            int idx = -1;
            in >> idx;
            std::string name = ReadQuoted(in);
            GameObject* go = scene.CreateGameObject(name);
            if (firstNew && !*firstNew) *firstNew = go;
            byIndex[idx] = go;

            std::string field;
            while (in >> field && field != "end") {
                if (field == "active") { int a = 1; in >> a; go->active = (a != 0); }
                else if (field == "tag") { go->tag = ReadQuoted(in); }
                else if (field == "parent") { int p = -1; in >> p; if (p >= 0) parentLinks.push_back({idx, p}); }
                else if (field == "transform") {
                    Vec3 p, s; Quat q;
                    in >> p.x >> p.y >> p.z >> q.x >> q.y >> q.z >> q.w >> s.x >> s.y >> s.z;
                    go->transform->localPosition = p;
                    go->transform->localRotation = q;
                    go->transform->localScale = s;
                } else if (field == "sprite") {
                    int glyph = 0; Color c; Vec2 size;
                    in >> glyph >> c.r >> c.g >> c.b >> c.a >> size.x >> size.y;
                    auto* sr = go->AddComponent<SpriteRenderer>();
                    sr->glyph = static_cast<char>(glyph);
                    sr->color = c;
                    sr->size = size;
                } else if (field == "camera") {
                    Color c; int proj = 0; float ortho = 5.0f, fov = 60.0f; int main = 1;
                    in >> proj >> ortho >> fov >> c.r >> c.g >> c.b >> c.a >> main;
                    auto* cam = go->AddComponent<Camera>();
                    cam->projection = (Camera::Projection)proj;
                    cam->orthographicSize = ortho;
                    cam->fieldOfView = fov;
                    cam->backgroundColor = c;
                    cam->main = (main != 0);
                } else if (field == "mesh") {
                    std::string kind = ReadQuoted(in);
                    Color c; int wire = 1;
                    in >> c.r >> c.g >> c.b >> c.a >> wire;
                    auto* mr = go->AddComponent<MeshRenderer>();
                    mr->mesh = Mesh::FromName(kind);
                    mr->color = c;
                    mr->wireframe = (wire != 0);
                } else if (field == "rigidbody2d") {
                    int bt = 0; float gs = 1, mass = 1, drag = 0, bounce = 0;
                    in >> bt >> gs >> mass >> drag >> bounce;
                    auto* rb = go->AddComponent<Rigidbody2D>();
                    rb->bodyType = (Rigidbody2D::BodyType)bt;
                    rb->gravityScale = gs; rb->mass = mass; rb->drag = drag; rb->bounciness = bounce;
                } else if (field == "boxcollider2d") {
                    Vec2 sz, off; int trig = 0;
                    in >> sz.x >> sz.y >> off.x >> off.y >> trig;
                    auto* bc = go->AddComponent<BoxCollider2D>();
                    bc->size = sz; bc->offset = off; bc->isTrigger = (trig != 0);
                } else if (field == "circlecollider2d") {
                    float r = 0.5f; Vec2 off; int trig = 0;
                    in >> r >> off.x >> off.y >> trig;
                    auto* cc = go->AddComponent<CircleCollider2D>();
                    cc->radius = r; cc->offset = off; cc->isTrigger = (trig != 0);
                } else if (field == "script") {
                    std::string lang = ReadQuoted(in);
                    std::string src  = ReadQuoted(in);
                    auto* sc = go->AddComponent<ScriptComponent>(lang);
                    sc->LoadSource(src);
                } else if (field == "scriptpath") {
                    std::string p = ReadQuoted(in);
                    if (auto* sc = go->GetComponent<ScriptComponent>()) sc->SetPath(p);
                } else if (field == "visualscript") {
                    std::string src = ReadQuoted(in);
                    auto* vsc = go->AddComponent<VisualScriptComponent>();
                    vsc->LoadFromText(src);
                } else {
                    if (error) *error = "unknown field '" + field + "'";
                    return false;
                }
            }
        } else {
            if (error) *error = "unexpected token '" + token + "'";
            return false;
        }
    }

    // Resolve parent links now that every object exists.
    for (auto& [childIdx, parentIdx] : parentLinks) {
        auto c = byIndex.find(childIdx);
        auto p = byIndex.find(parentIdx);
        if (c != byIndex.end() && p != byIndex.end())
            c->second->transform->SetParent(p->second->transform, /*worldPositionStays=*/false);
    }
    return true;
}

bool SceneSerializer::Deserialize(Scene& scene, const std::string& text, std::string* error) {
    return ParseInto(scene, text, /*clear=*/true, /*firstNew=*/nullptr, error);
}

std::string SceneSerializer::SerializeObject(const GameObject& root) {
    // Gather the object and all its descendants (depth-first).
    std::vector<GameObject*> subtree;
    std::function<void(GameObject*)> gather = [&](GameObject* go) {
        subtree.push_back(go);
        for (Transform* child : go->transform->Children()) gather(child->gameObject);
    };
    gather(const_cast<GameObject*>(&root));

    std::unordered_map<const GameObject*, int> localIndex;
    for (std::size_t i = 0; i < subtree.size(); ++i) localIndex[subtree[i]] = (int)i;

    std::ostringstream out;
    out << "okayscene 1\n";
    for (std::size_t i = 0; i < subtree.size(); ++i) {
        GameObject* go = subtree[i];
        out << "gameobject " << i << " " << Quote(go->name) << "\n";
        out << "  active " << (go->active ? 1 : 0) << "\n";
        if (!go->tag.empty()) out << "  tag " << Quote(go->tag) << "\n";
        Transform* parent = go->transform->Parent();
        int pIdx = -1;
        if (parent) {
            auto it = localIndex.find(parent->gameObject);
            if (it != localIndex.end()) pIdx = it->second;
        }
        out << "  parent " << pIdx << "\n";
        WriteComponents(out, go);
        out << "end\n";
    }
    return out.str();
}

GameObject* SceneSerializer::Instantiate(Scene& scene, const GameObject& prefab) {
    std::string text = SerializeObject(prefab);
    GameObject* root = nullptr;
    ParseInto(scene, text, /*clear=*/false, &root, nullptr);
    return root;
}

bool SceneSerializer::SaveObjectToFile(const GameObject& root, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << SerializeObject(root);
    return static_cast<bool>(f);
}

GameObject* SceneSerializer::InstantiateFromFile(Scene& scene, const std::string& path,
                                                 std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return nullptr; }
    std::stringstream ss; ss << f.rdbuf();
    GameObject* root = nullptr;
    if (!ParseInto(scene, ss.str(), /*clear=*/false, &root, error)) return nullptr;
    return root;
}

bool SceneSerializer::LoadFromFile(Scene& scene, const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    return Deserialize(scene, ss.str(), error);
}

} // namespace okay
