#include "okay/Debug/ProjectDiagnostics.hpp"

#include "okay/Core/AssetDatabase.hpp"
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/CharacterController2D.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/FollowTarget2D.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Minimap.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Components/NoCode.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Testing/ObjectPath.hpp"
#include "okay/Testing/SceneValidator.hpp"
#include "okay/VisualScript/VsTypes.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <typeindex>
#include <typeinfo>
#if defined(__GNUC__) || defined(__clang__)
#  include <cxxabi.h>
#endif

namespace okay {

namespace {

std::string Demangle(const char* raw) {
    std::string name = raw ? raw : "";
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    if (demangled) {
        if (status == 0) name = demangled;
        std::free(demangled);
    }
#endif
    for (const char* prefix : {"class ", "struct "}) {
        const std::size_t n = std::char_traits<char>::length(prefix);
        if (name.compare(0, n, prefix) == 0) name.erase(0, n);
    }
    if (name.compare(0, 6, "okay::") == 0) name.erase(0, 6);
    return name;
}

std::string Norm(std::string s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == ' ' || c == '_' || c == '-') continue;
        o.push_back((char)std::tolower((unsigned char)c));
    }
    const std::string tail = "component";
    if (o.size() > tail.size() && o.compare(o.size() - tail.size(), tail.size(), tail) == 0)
        o.erase(o.size() - tail.size());
    return o;
}

std::string HintFor(const std::string& code) {
    if (code == "scene.no_camera" || code == "scene.no_main_camera")
        return "Add a Camera and leave Main turned on. The Fix button adds a Main Camera.";
    if (code == "scene.no_active_camera")
        return "Enable a camera, and make sure its object is active.";
    if (code == "asset.missing" || code == "scene.missing_asset" || code == "asset.db.missing")
        return "Point the field at a file that exists, or clear it. Fix re-imports when one matching file is in the project, otherwise it clears the broken path.";
    if (code == "asset.prefab_missing")
        return "Put the prefab file back, or clear Prefab Source on the instance.";
    if (code == "script.missing_file")
        return "Choose a script file that exists, or clear the path and paste the script in. Fix clears the missing path.";
    if (code == "script.compile")
        return "Open the script and fix the error at the reported line.";
    if (code == "script.empty")
        return "Assign a script file, or write a script in the component.";
    if (code == "script.backend")
        return "This build does not include that script language. Switch the component to OkayScript.";
    if (code == "ref.missing_object")
        return "Pick an object that is in this scene, or clear the field. Fix clears it.";
    if (code == "component.missing_requirement")
        return "Add the component this one requires. Fix adds it.";
    if (code == "player.missing_collider")
        return "Add a collider so the player cannot pass through the world. Fix adds a box collider.";
    if (code == "player.missing_rigidbody")
        return "This controller moves a rigidbody. Fix adds one.";
    if (code == "animator.no_controller")
        return "Open the Animator window and add a state, or remove the Animator if this object does not animate.";
    if (code == "navmesh.no_bake")
        return "Open Navigation and bake a mesh, or add a Nav Mesh Surface that points at a baked .okaynav file.";
    if (code == "graph.error" || code == "graph.warning")
        return "Open the graph. The node with the badge is the one to fix, and a bad wire's pins are red.";
    if (code == "graph.node_requirement")
        return "Add the component this node uses, or remove the node.";
    if (code == "graph.parse")
        return "The graph text could not be read. Open it in the Visual Script editor.";
    if (code == "scene.duplicate_guid")
        return "Two objects share an id. Duplicate one of them so it gets its own.";
    if (code == "scene.broken_parent")
        return "The parent id does not match an object in the file. Reparent the object in the Hierarchy.";
    if (code == "scene.missing_component")
        return "This build does not know that component. Remove the record or open the scene in a build that has it.";
    if (code == "physics.rigidbody_no_collider")
        return "Add a collider, or the body falls through everything.";
    if (code == "transform.zero_scale")
        return "A scale of 0 flattens the object. Fix sets the zero axes to 1.";
    if (code == "transform.non_finite")
        return "A NaN or infinite value breaks everything below this object. Fix resets those values.";
    if (code == "geometry.zero_collider")
        return "Give the collider a size above zero (or turn on Auto Fit). Fix resets it to the default size.";
    if (code == "geometry.empty_mesh")
        return "Assign a mesh, or remove the Mesh Renderer if the object is not meant to draw anything.";
    if (code == "object.duplicate_overlap")
        return "Delete one of the two, or move it. Fix deletes the copy when it has no children and the same components as the original.";
    if (code == "lighting.light_leak")
        return "Overlap the two boxes a little, or add a filler box in the seam, then bake lighting again.";
    if (code == "nav.off_mesh")
        return "Move it onto the walkable area, or bake the navigation mesh again if the floor changed.";
    if (code == "nav.unreachable")
        return "Connect the two walkable regions (a ramp, a wider doorway) or move it into the region the agents are in.";
    if (code == "physics.moving_collider_no_rigidbody")
        return "Add a Kinematic Rigidbody so physics follows the movement. Fix adds one.";
    if (code == "scene.multiple_main_cameras")
        return "Keep Main on one camera only. Fix turns it off on this one.";
    if (code == "scene.no_light")
        return "Add a Light, or switch the materials to Unlit. Fix adds a Directional Light.";
    if (code == "name.duplicate")
        return "Rename one of them so Find() is not ambiguous.";
    return "Open the object this row points at and correct what it describes.";
}

int LineIn(const std::string& message) {
    const std::string key = "line ";
    std::size_t p = message.find(key);
    if (p == std::string::npos) return 0;
    return std::atoi(message.c_str() + p + key.size());
}

void QuotedAndField(const std::string& message, std::string& quoted, std::string& comp, std::string& field) {
    std::size_t a = message.find('\'');
    std::size_t b = a == std::string::npos ? std::string::npos : message.find('\'', a + 1);
    if (a != std::string::npos && b != std::string::npos && b > a + 1)
        quoted = message.substr(a + 1, b - a - 1);
    std::size_t close = message.rfind(')');
    std::size_t open = close == std::string::npos ? std::string::npos : message.rfind('(', close);
    if (open == std::string::npos || close == std::string::npos || close <= open + 1) return;
    std::string inner = message.substr(open + 1, close - open - 1);
    std::size_t dot = inner.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= inner.size()) return;
    comp = inner.substr(0, dot);
    field = inner.substr(dot + 1);
}

GameObject* FindObject(const Scene& scene, const std::string& path) {
    if (path.empty()) return nullptr;
    if (GameObject* g = scene.FindByPath(path)) return g;
    std::string name = path;
    std::size_t cut = name.find(" (#");
    if (cut != std::string::npos) name = name.substr(0, cut);
    return scene.Find(name);
}

void FillObject(DiagSource& src, const Scene& scene, const std::string& path) {
    src.objectPath = path;
    if (GameObject* go = FindObject(scene, path)) {
        src.objectGuid = go->Guid();
        src.objectPath = testing::ObjectPath(go);
    }
}

std::string UniqueFileNamed(const std::string& root, const std::string& filename) {
    if (root.empty() || filename.empty()) return {};
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return {};
    std::string found;
    int matches = 0;
    int seen = 0;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; it != end && seen < 4000; it.increment(ec)) {
        ++seen;
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        if (it->path().filename() == filename) {
            ++matches;
            found = fs::relative(it->path(), root, ec).generic_string();
            if (ec || found.empty()) found = it->path().generic_string();
            if (matches > 1) return {};
        }
    }
    return matches == 1 ? found : std::string{};
}

bool HasComponentNamed(const GameObject& go, const std::string& typeName) {
    const std::string want = Norm(typeName);
    if (want.empty()) return false;
    for (const auto& c : go.Components()) {
        if (!c) continue;
        if (Norm(testing::ComponentTypeName(*c)) == want) return true;
        if (Norm(ComponentSerializerRegistry::TypeKey(*c)) == want) return true;
    }
    return false;
}

struct NodeRec { int id = -1; std::string type; };

std::vector<NodeRec> NodesIn(const std::string& text) {
    std::vector<NodeRec> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string kind;
        ls >> kind;
        if (kind != "node") continue;
        NodeRec n;
        if (ls >> n.id >> n.type) out.push_back(n);
    }
    return out;
}

DiagSeverity FromTesting(testing::Severity s) {
    if (s == testing::Severity::Error) return DiagSeverity::Error;
    if (s == testing::Severity::Warning) return DiagSeverity::Warning;
    return DiagSeverity::Info;
}

void AddItem(std::vector<Diagnostic>& out, Diagnostic d) {
    if (d.hint.empty()) d.hint = HintFor(d.code);
    out.push_back(std::move(d));
}

bool AlreadyMissingAsset(const std::vector<Diagnostic>& items, const std::string& objectPath, const std::string& asset) {
    for (const Diagnostic& d : items) {
        if (d.code != "asset.missing" && d.code != "scene.missing_asset") continue;
        if (!asset.empty() && d.source.assetPath == asset) return true;
        if (!objectPath.empty() && d.source.objectPath == objectPath && d.message.find(asset) != std::string::npos)
            return true;
    }
    return false;
}

void MapSemantic(const Scene& scene, const testing::ValidationReport& report, const ProjectDiagOptions& opt,
                 std::vector<Diagnostic>& out) {
    for (const testing::ValidationIssue& issue : report.issues) {
        Diagnostic d;
        d.severity = FromTesting(issue.severity);
        d.code = issue.code;
        d.message = issue.message;
        d.source.component = issue.component;
        FillObject(d.source, scene, issue.object);
        if (!issue.guid.empty()) {   // the validator knows the exact object: sibling names can repeat
            if (GameObject* g = scene.FindByGuid(issue.guid)) {
                d.source.objectGuid = g->Guid();
                d.source.objectPath = testing::ObjectPath(g);
            }
        }
        d.relatedGuids = issue.relatedGuids;
        if (issue.code == "transform.zero_scale") d.fix.kind = DiagFixKind::ResetScale;
        else if (issue.code == "transform.non_finite") d.fix.kind = DiagFixKind::ResetTransform;
        else if (issue.code == "geometry.zero_collider") d.fix.kind = DiagFixKind::ResetColliderSize;
        else if (issue.code == "physics.moving_collider_no_rigidbody") d.fix.kind = DiagFixKind::AddKinematicBody;
        else if (issue.code == "scene.multiple_main_cameras") d.fix.kind = DiagFixKind::ClearMainCamera;
        else if (issue.code == "scene.no_light") d.fix.kind = DiagFixKind::AddLight;
        else if (issue.code == "object.duplicate_overlap") {
            GameObject* dup = d.source.objectGuid.empty() ? nullptr : scene.FindByGuid(d.source.objectGuid);
            GameObject* orig = issue.relatedGuids.empty() ? nullptr : scene.FindByGuid(issue.relatedGuids[0]);
            // Only a copy that carries nothing the original lacks is safe to drop in one click.
            if (dup && orig && dup->transform->Children().empty() && dup->Components().size() == orig->Components().size() &&
                dup->prefabSource == orig->prefabSource)
                d.fix.kind = DiagFixKind::DeleteObject;
        }
        if (issue.code == "scene.no_camera") {
            d.severity = DiagSeverity::Error;
            d.message = "This scene has no main camera, so nothing is rendered.";
            d.fix.kind = DiagFixKind::AddCamera;
            d.hint = HintFor(d.code);
        } else if (issue.code == "asset.missing" || issue.code == "script.missing_file" || issue.code == "ref.missing_object") {
            std::string quoted, comp, field;
            QuotedAndField(issue.message, quoted, comp, field);
            if (!comp.empty()) d.source.component = comp;
            if (!field.empty()) d.source.field = field;
            if (issue.code == "asset.missing") {
                d.source.assetPath = quoted;
                std::string file = quoted;
                std::size_t slash = file.find_last_of("/\\");
                if (slash != std::string::npos) file = file.substr(slash + 1);
                std::string found = UniqueFileNamed(opt.assetRoot, file);
                if (!found.empty()) {
                    d.fix.kind = DiagFixKind::Reimport;
                    d.fix.arg = found;
                } else {
                    d.fix.kind = DiagFixKind::ClearReference;
                }
            } else if (issue.code == "script.missing_file") {
                d.source.scriptPath = quoted;
                d.fix.kind = DiagFixKind::ClearReference;
                if (d.source.field.empty()) d.source.field = "path";
                if (d.source.component.empty()) d.source.component = "ScriptComponent";
            } else {
                d.fix.kind = DiagFixKind::ClearReference;
                if (d.source.field.empty()) {
                    std::size_t dot = issue.message.find('.');
                    std::size_t sp = issue.message.find(' ');
                    if (dot != std::string::npos && sp != std::string::npos && dot < sp)
                        d.source.field = issue.message.substr(dot + 1, sp - dot - 1);
                }
            }
        } else if (issue.code == "script.compile" || issue.code == "script.empty" || issue.code == "script.backend") {
            d.source.scriptLine = LineIn(issue.message);
            if (GameObject* go = FindObject(scene, issue.object)) {
                if (ScriptComponent* sc = go->GetComponent<ScriptComponent>()) {
                    d.source.scriptPath = sc->Path();
                    d.source.component = "ScriptComponent";
                    if (d.source.scriptLine <= 0) d.source.scriptLine = LineIn(issue.message);
                }
            }
        }
        AddItem(out, std::move(d));
    }
}

void MapSceneFile(const Scene& scene, const SceneValidationReport& report, std::vector<Diagnostic>& out) {
    for (const SceneIssue& issue : report.issues) {
        Diagnostic d;
        d.severity = DiagSeverity::Error;
        switch (issue.kind) {
            case SceneIssue::Kind::MissingComponentType: d.code = "scene.missing_component"; break;
            case SceneIssue::Kind::UnknownField:        d.code = "scene.unknown_field"; d.severity = DiagSeverity::Warning; break;
            case SceneIssue::Kind::MissingAsset:        d.code = "scene.missing_asset"; break;
            case SceneIssue::Kind::BrokenParentLink:    d.code = "scene.broken_parent"; break;
            case SceneIssue::Kind::DuplicateGuid:       d.code = "scene.duplicate_guid"; break;
            case SceneIssue::Kind::LoadError:           d.code = "scene.load_error"; break;
            case SceneIssue::Kind::UnknownTag:          d.code = "scene.unknown_tag"; d.severity = DiagSeverity::Warning; break;
            case SceneIssue::Kind::BadLayer:            d.code = "scene.bad_layer"; d.severity = DiagSeverity::Warning; break;
        }
        d.message = issue.message;
        FillObject(d.source, scene, issue.object);
        if (issue.kind == SceneIssue::Kind::MissingAsset) {
            std::string quoted, comp, field;
            QuotedAndField(issue.message, quoted, comp, field);
            if (quoted.empty()) {
                const std::size_t c = issue.message.find(": ");
                if (c != std::string::npos) quoted = issue.message.substr(c + 2);
                while (!quoted.empty() && (quoted.back() == '\r' || quoted.back() == ' ')) quoted.pop_back();
            }
            if (AlreadyMissingAsset(out, d.source.objectPath, quoted)) continue;
            d.source.assetPath = quoted;
        }
        AddItem(out, std::move(d));
    }
}

void CheckRequirements(const Scene& scene, std::vector<Diagnostic>& out) {
    if (!ComponentRules::Any()) return;
    for (const auto& up : scene.Objects()) {
        GameObject& go = *up;
        if (go.IsDestroyed() || go.transient) continue;
        std::vector<Component*> cs;
        for (const auto& c : go.Components()) if (c) cs.push_back(c.get());
        for (Component* c : cs) {
            const auto reqs = ComponentRules::RequirementsOf(typeid(*c));
            for (const ComponentRules::Requirement& req : reqs) {
                if (req.has(go)) continue;
                Diagnostic d;
                d.severity = DiagSeverity::Error;
                d.code = "component.missing_requirement";
                const std::string owner = testing::ComponentTypeName(*c);
                const std::string need = Demangle(req.type.name());
                d.message = owner + " needs a " + need + " on this object, and it is missing.";
                d.source.objectGuid = go.Guid();
                d.source.objectPath = testing::ObjectPath(&go);
                d.source.component = owner;
                d.source.field = need;
                d.fix.kind = DiagFixKind::AddComponent;
                d.fix.arg = "*";
                AddItem(out, std::move(d));
            }
        }
    }
}

void AddBody(std::vector<Diagnostic>& out, GameObject& go, const char* code, const std::string& message,
             const char* component, const char* addType) {
    Diagnostic d;
    d.severity = DiagSeverity::Warning;
    d.code = code;
    d.message = message;
    d.source.objectGuid = go.Guid();
    d.source.objectPath = testing::ObjectPath(&go);
    d.source.component = component;
    d.fix.kind = DiagFixKind::AddComponent;
    d.fix.arg = addType;
    AddItem(out, std::move(d));
}

void CheckGameplay(const Scene& scene, std::vector<Diagnostic>& out) {
    bool anyCamera = false, anyMain = false;
    for (const auto& up : scene.Objects()) {
        if (!up || up->IsDestroyed()) continue;
        if (Camera* cam = up->GetComponent<Camera>()) {
            anyCamera = true;
            if (cam->main && cam->enabled && up->active) anyMain = true;
        }
    }
    if (anyCamera && !anyMain) {
        Diagnostic d;
        d.severity = DiagSeverity::Warning;
        d.code = "scene.no_main_camera";
        d.message = "Cameras are in the scene, but none is marked Main, so the game has no main camera.";
        d.fix.kind = DiagFixKind::MakeMainCamera;
        for (const auto& up : scene.Objects()) {
            if (up && up->GetComponent<Camera>()) {
                d.source.objectGuid = up->Guid();
                d.source.objectPath = testing::ObjectPath(up.get());
                d.source.component = "Camera";
                break;
            }
        }
        AddItem(out, std::move(d));
    }

    bool navOk = false;
    for (NavMeshSurface* surface : scene.FindObjectsOfType<NavMeshSurface>()) {
        if (surface && (surface->HasMesh() || !surface->navPath.empty())) navOk = true;
    }
    for (const auto& up : scene.Objects()) {
        GameObject& go = *up;
        if (go.IsDestroyed() || go.transient) continue;
        const bool player3d = go.GetComponent<CharacterController3D>() || go.GetComponent<FirstPersonController>() ||
                              go.GetComponent<ThirdPersonController>() || go.GetComponent<PlayerController>();
        if (player3d) {
            if (!go.GetComponent<Collider3D>())
                AddBody(out, go, "player.missing_collider",
                        "This player controller has no collider, so it passes through the world.",
                        "CharacterController3D", "BoxCollider3D");
            if (!go.GetComponent<Rigidbody3D>())
                AddBody(out, go, "player.missing_rigidbody",
                        "This player controller needs a rigidbody to move and to stand on the ground.",
                        "CharacterController3D", "Rigidbody3D");
        }
        if (CharacterController2D* cc = go.GetComponent<CharacterController2D>()) {
            if (cc->mode == CharacterController2D::Mode::Platformer) {
                if (!go.GetComponent<Collider2D>())
                    AddBody(out, go, "player.missing_collider",
                            "This platformer controller has no collider, so it passes through the ground.",
                            "CharacterController2D", "BoxCollider2D");
                if (!go.GetComponent<Rigidbody2D>())
                    AddBody(out, go, "player.missing_rigidbody",
                            "This platformer controller needs a 2D rigidbody to fall and to land.",
                            "CharacterController2D", "Rigidbody2D");
            }
        }
        if (AnimStateMachine* anim = go.GetComponent<AnimStateMachine>()) {
            if (anim->states.empty()) {
                Diagnostic d;
                d.severity = DiagSeverity::Warning;
                d.code = "animator.no_controller";
                d.message = "This Animator has no states, so it has nothing to play.";
                d.source.objectGuid = go.Guid();
                d.source.objectPath = testing::ObjectPath(&go);
                d.source.component = "AnimStateMachine";
                AddItem(out, std::move(d));
            }
        }
        if (!navOk && go.GetComponent<NavMeshAgent>()) {
            Diagnostic d;
            d.severity = DiagSeverity::Warning;
            d.code = "navmesh.no_bake";
            d.message = "This Nav Mesh Agent has no baked mesh to walk on.";
            d.source.objectGuid = go.Guid();
            d.source.objectPath = testing::ObjectPath(&go);
            d.source.component = "NavMeshAgent";
            AddItem(out, std::move(d));
        }
    }
}

void CheckGraphs(const Scene& scene, const ProjectDiagOptions& opt, std::vector<Diagnostic>& out) {
    for (const auto& up : scene.Objects()) {
        GameObject& go = *up;
        if (go.IsDestroyed() || go.transient) continue;
        for (VisualScriptComponent* vs : go.GetComponents<VisualScriptComponent>()) {
            const std::string text = vs->Source();
            if (text.empty()) continue;
            const std::vector<vs::VsIssue> issues = vs::ValidateGraphText(text, opt.assetRoot);
            const std::vector<NodeRec> nodes = NodesIn(text);
            auto typeOf = [&](int id) {
                for (const NodeRec& n : nodes) if (n.id == id) return n.type;
                return std::string();
            };
            for (const vs::VsIssue& issue : issues) {
                Diagnostic d;
                d.severity = issue.severity == 0 ? DiagSeverity::Error : DiagSeverity::Warning;
                d.code = issue.node < 0 ? "graph.parse" : (issue.severity == 0 ? "graph.error" : "graph.warning");
                const std::string nodeType = typeOf(issue.node);
                d.message = issue.message;
                if (issue.node >= 0) {
                    d.message = "Node " + (nodeType.empty() ? std::string("#") + std::to_string(issue.node) : nodeType) +
                                ": " + issue.message;
                    d.source.nodeId = std::to_string(issue.node);
                }
                d.source.objectGuid = go.Guid();
                d.source.objectPath = testing::ObjectPath(&go);
                d.source.component = "VisualScriptComponent";
                d.source.graphPath = vs->Path();
                d.source.scriptPath = vs->Path();
                AddItem(out, std::move(d));
            }
            for (const NodeRec& n : nodes) {
                for (const NodeRequirement& req : NodeRequirementsFor(n.type)) {
                    if (HasComponentNamed(go, req.component)) continue;
                    Diagnostic d;
                    d.severity = DiagSeverity::Warning;
                    d.code = "graph.node_requirement";
                    d.message = "Node " + n.type + " needs a " + req.component + " on this object.";
                    d.hint = req.hint.empty() ? HintFor(d.code) : req.hint;
                    d.source.objectGuid = go.Guid();
                    d.source.objectPath = testing::ObjectPath(&go);
                    d.source.component = "VisualScriptComponent";
                    d.source.field = req.component;
                    d.source.graphPath = vs->Path();
                    d.source.nodeId = std::to_string(n.id);
                    d.fix.kind = DiagFixKind::AddComponent;
                    d.fix.arg = req.component;
                    AddItem(out, std::move(d));
                }
            }
        }
    }
}

std::string* Field(GameObject& go, const std::string& component, const std::string& field) {
    const std::string c = Norm(component), f = Norm(field);
    if ((c == "spriterenderer" || c == "sprite") && (f == "texture" || f.empty()))
        if (SpriteRenderer* s = go.GetComponent<SpriteRenderer>()) return &s->texture;
    if ((c == "meshrenderer" || c == "mesh") && (f == "mesh" || f == "meshpath" || f.empty()))
        if (MeshRenderer* m = go.GetComponent<MeshRenderer>()) return &m->meshPath;
    if ((c == "audiosource" || c == "audio") && (f == "clip" || f == "clippath" || f.empty()))
        if (AudioSource* a = go.GetComponent<AudioSource>()) return &a->clipPath;
    if (c == "followtarget2d" && (f == "target" || f.empty()))
        if (FollowTarget2D* t = go.GetComponent<FollowTarget2D>()) return &t->target;
    if (c == "camerafollow" && (f == "targetname" || f == "target" || f.empty()))
        if (CameraFollow* t = go.GetComponent<CameraFollow>()) return &t->targetName;
    if (c == "minimap" && (f == "target" || f.empty()))
        if (Minimap* t = go.GetComponent<Minimap>()) return &t->target;
    if (c == "teleporter" && (f == "targetname" || f == "target" || f.empty()))
        if (Teleporter* t = go.GetComponent<Teleporter>()) return &t->targetName;
    if (c == "triggerzone" && (f == "targetname" || f == "target" || f.empty()))
        if (TriggerZone* t = go.GetComponent<TriggerZone>()) return &t->targetName;
    return nullptr;
}

bool ClearOrSet(GameObject& go, const std::string& component, const std::string& field, const std::string* value) {
    if (std::string* slot = Field(go, component, field)) {
        *slot = value ? *value : std::string();
        return true;
    }
    const std::string c = Norm(component);
    if (c == "scriptcomponent" || c == "script") {
        if (ScriptComponent* sc = go.GetComponent<ScriptComponent>()) {
            sc->SetPath(value ? *value : std::string());
            return true;
        }
    }
    if (c == "visualscriptcomponent" || c == "visualscript") {
        if (VisualScriptComponent* vs = go.GetComponent<VisualScriptComponent>()) {
            vs->SetPath(value ? *value : std::string());
            return true;
        }
    }
    return false;
}

bool AddNamed(GameObject& go, const std::string& type) {
    const std::string n = Norm(type);
    if (n == "boxcollider3d") { go.AddComponent<BoxCollider3D>(); return true; }
    if (n == "boxcollider2d") { go.AddComponent<BoxCollider2D>(); return true; }
    if (n == "rigidbody3d") { go.AddComponent<Rigidbody3D>(); return true; }
    if (n == "rigidbody2d") { go.AddComponent<Rigidbody2D>(); return true; }
    if (n == "camera") { go.AddComponent<Camera>(); return true; }
    if (n == "navmeshsurface") { go.AddComponent<NavMeshSurface>(); return true; }
    return false;
}

bool AddMissingRequirements(GameObject& go) {
    if (!ComponentRules::Any()) return false;
    std::vector<Component*> cs;
    for (const auto& c : go.Components()) if (c) cs.push_back(c.get());
    bool any = false;
    for (Component* c : cs) {
        const auto reqs = ComponentRules::RequirementsOf(typeid(*c));
        for (const ComponentRules::Requirement& req : reqs) {
            if (!req.has(go)) { req.add(go); any = true; }
        }
    }
    return any;
}

// References to files that are not in the project. Reads the project's text assets directly (scenes, prefabs,
// materials, clips, ...) and never writes: an earlier version indexed the folder with the asset database, which
// wrote a .okaymeta next to every file it looked at.
void AppendAssetIndex(const ProjectDiagOptions& options, std::vector<Diagnostic>& items) {
    if (!options.checkAssetIndex || options.assetRoot.empty()) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::u8path(options.assetRoot);
    if (!fs::is_directory(root, ec)) return;
    const std::string rootName = [&] {
        std::string n = root.filename().generic_u8string();
        if (n.empty() || n == ".") n = root.parent_path().filename().generic_u8string();
        for (char& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return n;
    }();
    static const char* kText[] = {"okayscene", "okayprefab", "okaymat", "okayanim", "okayclip", "okaydialog", "okayquest", "okaybt",
                                  "okayvs", "okayseq", "okayloot", "okayitems", "okayweapon", "okayrecipes", "okaydata", "okaynav", "gltf"};
    static const char* kSkip[] = {"build", "build-tests", "build-editor", ".git"};
    auto exists = [&](const std::string& raw, const std::string& referrerDir) {
        std::string n = raw;
        for (char& ch : n) if (ch == '\\') ch = '/';
        if (n.empty()) return true;
        std::error_code e;
        if (n[0] == '/' || (n.size() > 1 && n[1] == ':')) return fs::exists(fs::u8path(n), e);
        std::vector<std::string> tries{n};
        if (!referrerDir.empty()) tries.push_back(referrerDir + "/" + n);
        const auto s = n.find('/');
        if (s != std::string::npos) {
            std::string first = n.substr(0, s);
            for (char& ch : first) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (first == rootName || first == "assets") tries.push_back(n.substr(s + 1));
        }
        for (const std::string& t : tries) if (fs::exists(root / fs::u8path(t), e)) return true;
        return false;
    };
    int files = 0;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e1;
        const std::string name = it->path().filename().generic_u8string();
        if (it->is_directory(e1)) {
            bool skip = name.empty() || name[0] == '.';
            for (const char* k : kSkip) if (name == k) skip = true;
            if (skip || it->is_symlink(e1)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(e1) || name.empty() || name[0] == '.') continue;
        std::string ext = it->path().extension().generic_u8string();
        if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
        for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        bool text = false;
        for (const char* k : kText) if (ext == k) text = true;
        if (!text) continue;
        if (++files > 20000) break;
        const auto size = fs::file_size(it->path(), e1);
        if (e1 || size > (16u << 20)) continue;
        std::ifstream in(it->path(), std::ios::binary);
        if (!in) continue;
        std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<FoundRef> refs;
        if (ext == "okayscene" || ext == "okayprefab") refs = ExtractSceneRefs(body);
        else if (ext == "okaymat") refs = ExtractMaterialRefs(body);
        else refs = ExtractTextRefs(body);
        const std::string rel = it->path().lexically_relative(root).generic_u8string();
        const std::size_t sl = rel.find_last_of('/');
        const std::string dir = sl == std::string::npos ? std::string() : rel.substr(0, sl);
        std::set<std::string> seen;
        for (const FoundRef& r : refs) {
            if (r.path.empty() || r.path.find("://") != std::string::npos || r.path.rfind("proc:", 0) == 0) continue;
            if (!seen.insert(r.path).second || exists(r.path, dir)) continue;
            if (AlreadyMissingAsset(items, "", r.path)) continue;
            Diagnostic d;
            d.severity = DiagSeverity::Error;
            d.code = "asset.db.missing";
            d.message = rel + " references '" + r.path + "', and that file is not in the project.";
            d.source.assetPath = r.path;
            d.source.scriptPath = rel;
            d.hint = HintFor(d.code);
            items.push_back(std::move(d));
        }
    }
}

} // namespace

ProjectDiagResult CollectProjectDiagnostics(const Scene& scene, const ProjectDiagOptions& options) {
    ProjectDiagResult result;
    if (options.checkSemantic) {
        testing::ValidatorOptions vo;
        vo.checkLevel = options.checkLevelDesign;
        vo.assetRoot = options.assetRoot;
        vo.fileExists = options.fileExists;
        vo.checkInactive = false;
        vo.noCameraSeverity = testing::Severity::Error;
        MapSemantic(scene, testing::SceneValidator::Validate(scene, vo), options, result.items);
    }
    if (options.checkSceneFile)
        MapSceneFile(scene, scene.Validate(options.assetRoot), result.items);
    if (options.checkRequirements) CheckRequirements(scene, result.items);
    if (options.checkGameplay) CheckGameplay(scene, result.items);
    if (options.checkGraphs) CheckGraphs(scene, options, result.items);
    AppendAssetIndex(options, result.items);
    return result;
}

bool ApplyDiagnosticFix(Scene& scene, const Diagnostic& d, std::string* error) {
    auto fail = [&](const char* m) {
        if (error) *error = m;
        return false;
    };
    GameObject* go = nullptr;
    if (!d.source.objectGuid.empty()) go = scene.FindByGuid(d.source.objectGuid);
    if (!go && !d.source.objectPath.empty()) go = FindObject(scene, d.source.objectPath);

    switch (d.fix.kind) {
        case DiagFixKind::None:
            return fail("There is no automatic fix for this.");
        case DiagFixKind::AddCamera: {
            GameObject* cam = scene.CreateGameObject("Main Camera");
            cam->AddComponent<Camera>()->main = true;
            if (cam->transform) cam->transform->SetPosition({0, 2, 10});
            return true;
        }
        case DiagFixKind::MakeMainCamera: {
            if (!go) return fail("That object is no longer in the scene.");
            Camera* cam = go->GetComponent<Camera>();
            if (!cam) return fail("That object has no Camera.");
            cam->main = true;
            return true;
        }
        case DiagFixKind::ClearMainCamera: {
            if (!go) return fail("That object is no longer in the scene.");
            Camera* cam = go->GetComponent<Camera>();
            if (!cam) return fail("That object has no Camera.");
            cam->main = false;
            return true;
        }
        case DiagFixKind::AddLight: {
            for (Light* l : scene.FindObjectsOfType<Light>()) {
                // "No active light" with lights that exist: turn the first one back on instead of adding another.
                if (l && l->gameObject) {
                    l->enabled = true;
                    l->gameObject->active = true;
                    return true;
                }
            }
            GameObject* sun = scene.CreateGameObject("Directional Light");
            sun->AddComponent<Light>();
            if (sun->transform) sun->transform->SetEulerAngles({50.0f, -30.0f, 0.0f});
            return true;
        }
        case DiagFixKind::ResetScale: {
            if (!go || !go->transform) return fail("That object is no longer in the scene.");
            Vec3& sc = go->transform->localScale;
            if (sc.x != 0 && sc.y != 0 && sc.z != 0) return fail("The scale is no longer zero.");
            if (sc.x == 0) sc.x = 1.0f;
            if (sc.y == 0) sc.y = 1.0f;
            if (sc.z == 0) sc.z = 1.0f;
            return true;
        }
        case DiagFixKind::ResetTransform: {
            if (!go || !go->transform) return fail("That object is no longer in the scene.");
            Transform& t = *go->transform;
            auto fix = [](float& v, float def) { if (!std::isfinite(v)) { v = def; return true; } return false; };
            bool any = false;
            any |= fix(t.localPosition.x, 0); any |= fix(t.localPosition.y, 0); any |= fix(t.localPosition.z, 0);
            any |= fix(t.localScale.x, 1); any |= fix(t.localScale.y, 1); any |= fix(t.localScale.z, 1);
            if (!std::isfinite(t.localRotation.x) || !std::isfinite(t.localRotation.y) ||
                !std::isfinite(t.localRotation.z) || !std::isfinite(t.localRotation.w)) {
                t.localRotation = Quat::Identity;
                any = true;
            }
            if (!any) return fail("The transform is already finite.");
            return true;
        }
        case DiagFixKind::ResetColliderSize: {
            if (!go) return fail("That object is no longer in the scene.");
            bool any = false;
            for (BoxCollider3D* b : go->GetComponents<BoxCollider3D>()) {
                if (b->size.x <= 0) { b->size.x = 1; any = true; }
                if (b->size.y <= 0) { b->size.y = 1; any = true; }
                if (b->size.z <= 0) { b->size.z = 1; any = true; }
            }
            for (SphereCollider3D* b : go->GetComponents<SphereCollider3D>()) if (b->radius <= 0) { b->radius = 0.5f; any = true; }
            for (CapsuleCollider3D* b : go->GetComponents<CapsuleCollider3D>()) {
                if (b->radius <= 0) { b->radius = 0.5f; any = true; }
                if (b->height <= 0) { b->height = 2.0f; any = true; }
            }
            for (BoxCollider2D* b : go->GetComponents<BoxCollider2D>()) {
                if (b->size.x <= 0) { b->size.x = 1; any = true; }
                if (b->size.y <= 0) { b->size.y = 1; any = true; }
            }
            for (CircleCollider2D* b : go->GetComponents<CircleCollider2D>()) if (b->radius <= 0) { b->radius = 0.5f; any = true; }
            if (!any) return fail("The collider no longer has a zero size.");
            return true;
        }
        case DiagFixKind::AddKinematicBody: {
            if (!go) return fail("That object is no longer in the scene.");
            bool solid3 = false;
            for (Collider3D* c : go->GetComponents<Collider3D>()) if (!c->isTrigger) solid3 = true;
            if (solid3) {
                if (go->GetComponent<Rigidbody3D>()) return fail("That object already has a Rigidbody.");
                go->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Kinematic;
            } else {
                if (go->GetComponent<Rigidbody2D>()) return fail("That object already has a Rigidbody.");
                go->AddComponent<Rigidbody2D>()->bodyType = Rigidbody2D::BodyType::Kinematic;
            }
            return true;
        }
        case DiagFixKind::DeleteObject: {
            if (!go) return fail("That object is no longer in the scene.");
            if (!go->transform->Children().empty()) return fail("That object has children, so it is not deleted automatically.");
            scene.Destroy(go);
            scene.FlushDestroyed();
            return true;
        }
        case DiagFixKind::AddComponent: {
            if (!go) return fail("That object is no longer in the scene.");
            if (d.fix.arg == "*" || d.fix.arg.empty()) {
                if (!AddMissingRequirements(*go)) return fail("That component is no longer missing.");
                return true;
            }
            if (!AddNamed(*go, d.fix.arg)) return fail("That component cannot be added from here.");
            return true;
        }
        case DiagFixKind::ClearReference: {
            if (!go) return fail("That object is no longer in the scene.");
            if (!ClearOrSet(*go, d.source.component, d.source.field, nullptr))
                return fail("That reference cannot be cleared from here.");
            return true;
        }
        case DiagFixKind::Reimport: {
            if (!go) return fail("That object is no longer in the scene.");
            if (d.fix.arg.empty()) return fail("The file is not on disk, so there is nothing to re-import.");
            if (!ClearOrSet(*go, d.source.component, d.source.field, &d.fix.arg))
                return fail("That field cannot be re-imported from here.");
            return true;
        }
    }
    return fail("There is no automatic fix for this.");
}

} // namespace okay
