#pragma once
// Helpers for the friendly gameplay blocks of tools/gameplay_nodes_batch7.py (camera-relative
// directions, character speed and stance, vehicles, menus, colliders, debug shapes, watch values).
// Each one calls the engine component or system that already does the job (the same calls the
// engine API bindings in engine/src/Script/EngineApi.cpp and the text-script builtins make); none
// of them is a second copy of engine logic. Written for this project from the engine's own
// headers; no outside code.
#include "okay/Components/Camera.hpp"
#include "okay/Components/CharacterController2D.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleSeat.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace okay::vs {

/// The camera the player looks through: the scene's main camera, else the first camera in the scene.
inline Camera* FrViewCamera(Scene* s) {
    if (!s) return nullptr;
    if (s->mainCamera && s->mainCamera->gameObject && s->mainCamera->gameObject->active) return s->mainCamera;
    for (const auto& up : s->Objects())
        if (up && up->active)
            if (auto* c = up->GetComponent<Camera>()) return c;
    return nullptr;
}

/// The camera's view direction (`right` false) or its right (`right` true), flattened onto the ground
/// and one unit long, for moving a character relative to the camera. A camera looks along its local -Z
/// (the same convention VirtualCamera::ViewForward uses). Straight down or up: the camera's up is used
/// for "forward" so the answer is never zero.
inline Vec3 FrCameraFlat(Camera* cam, bool right) {
    if (!cam || !cam->transform) return Vec3{0, 0, right ? 0.0f : 1.0f};
    const Quat q = cam->transform->Rotation();
    Vec3 d = right ? q * Vec3{1.0f, 0.0f, 0.0f} : q * Vec3{0.0f, 0.0f, -1.0f};
    d.y = 0.0f;
    float len = std::sqrt(d.x * d.x + d.z * d.z);
    if (len < 1e-4f && !right) { d = q * Vec3{0.0f, 1.0f, 0.0f}; d.y = 0.0f; len = std::sqrt(d.x * d.x + d.z * d.z); }
    if (len < 1e-4f) return Vec3{right ? 1.0f : 0.0f, 0.0f, right ? 0.0f : 1.0f};
    return Vec3{d.x / len, 0.0f, d.z / len};
}

/// The walk and run speed fields of every player controller on `go` (first person, third person,
/// character controller 3D and 2D, top-down). A Player Controller holds a first- and a third-person
/// part, so both are listed.
struct FrSpeeds { std::vector<float*> walk, run; };
inline FrSpeeds FrMoverSpeeds(GameObject* go) {
    FrSpeeds s;
    if (!go) return s;
    if (auto* c = go->GetComponent<FirstPersonController>()) { s.walk.push_back(&c->walkSpeed); s.run.push_back(&c->runSpeed); }
    if (auto* c = go->GetComponent<ThirdPersonController>()) { s.walk.push_back(&c->walkSpeed); s.run.push_back(&c->runSpeed); }
    if (auto* c = go->GetComponent<CharacterController3D>()) { s.walk.push_back(&c->speed); s.run.push_back(&c->runSpeed); }
    if (auto* c = go->GetComponent<CharacterController2D>()) { s.walk.push_back(&c->speed); s.run.push_back(&c->runSpeed); }
    if (auto* c = go->GetComponent<TopDownController>()) { s.walk.push_back(&c->walkSpeed); s.run.push_back(&c->runSpeed); }
    return s;
}
/// Walk speed `speed` on every controller; the run speed keeps its ratio to the walk speed.
inline bool FrSetMoveSpeed(GameObject* go, float speed) {
    FrSpeeds s = FrMoverSpeeds(go);
    if (s.walk.empty()) return false;
    if (!std::isfinite(speed) || speed < 0.0f) speed = 0.0f;
    for (std::size_t i = 0; i < s.walk.size(); ++i) {
        const float w = *s.walk[i];
        const float ratio = w > 1e-4f ? *s.run[i] / w : 1.6f;
        *s.walk[i] = speed;
        *s.run[i] = speed * ratio;
    }
    return true;
}

/// Run `f` on the first- or third-person controller of `go` or of the nearest parent that has one
/// (the same search the text-script `crouch` builtin makes). False when there is none.
template <class F> inline bool FrWithStanceController(GameObject* go, F&& f) {
    for (Transform* t = go ? go->transform : nullptr; t; t = t->Parent()) {
        GameObject* p = t->gameObject;
        if (!p) continue;
        bool any = false;
        if (auto* c = p->GetComponent<ThirdPersonController>()) { f(c); any = true; }
        if (auto* c = p->GetComponent<FirstPersonController>()) { f(c); any = true; }
        if (any) return true;
    }
    return false;
}

/// The seat `who` sits in, or nullptr.
inline VehicleSeat* FrSeatOf(GameObject* who) {
    Scene* s = who ? who->scene() : nullptr;
    if (!s) return nullptr;
    for (const auto& up : s->Objects())
        if (up)
            for (VehicleSeat* seat : up->GetComponents<VehicleSeat>())
                if (seat->Occupant() == who) return seat;
    return nullptr;
}
/// Put `who` in `vehicle`: its free driver seat first, then a free passenger seat.
inline bool FrEnterVehicle(GameObject* vehicle, GameObject* who) {
    if (!vehicle || !who) return false;
    std::vector<VehicleSeat*> seats = vehicle->GetComponentsInChildren<VehicleSeat>(false);
    for (int pass = 0; pass < 2; ++pass)
        for (VehicleSeat* seat : seats)
            if (seat && seat->enabled && seat->driver == (pass == 0) && !seat->Occupant() && seat->Enter(who)) return true;
    return false;
}
/// True while `who` drives a vehicle (sits in a driver seat).
inline bool FrIsDriving(GameObject* who) {
    Scene* s = who ? who->scene() : nullptr;
    if (!s) return false;
    for (const auto& up : s->Objects())
        if (up)
            if (auto* v = up->GetComponent<VehicleController>())
                if (v->Driver() == who) return true;
    return false;
}

/// Size every collider on `go`: a box gets Width x Height x Depth, a sphere or circle radius Width / 2,
/// a capsule radius Width / 2 and Height. Returns how many colliders were changed.
inline int FrSetColliderSize(GameObject* go, float w, float h, float d) {
    if (!go) return 0;
    w = std::max(0.0f, w); h = std::max(0.0f, h); d = std::max(0.0f, d);
    int n = 0;
    for (const auto& c : go->Components()) {
        if (auto* b = dynamic_cast<BoxCollider3D*>(c.get())) { b->size = Vec3{w, h, d}; ++n; }
        else if (auto* sp = dynamic_cast<SphereCollider3D*>(c.get())) { sp->radius = w * 0.5f; ++n; }
        else if (auto* ca = dynamic_cast<CapsuleCollider3D*>(c.get())) { ca->radius = w * 0.5f; ca->height = h; ++n; }
        else if (auto* b2 = dynamic_cast<BoxCollider2D*>(c.get())) { b2->size = Vec2{w, h}; ++n; }
        else if (auto* ci = dynamic_cast<CircleCollider2D*>(c.get())) { ci->radius = w * 0.5f; ++n; }
        else if (auto* c2 = dynamic_cast<CapsuleCollider2D*>(c.get())) { c2->size = Vec2{w, h}; ++n; }
    }
    return n;
}
/// Switch every collider on `go` on or off. Returns how many there are.
inline int FrSetCollidersEnabled(GameObject* go, bool on) {
    if (!go) return 0;
    int n = 0;
    for (const auto& c : go->Components())
        if (dynamic_cast<Collider3D*>(c.get()) || dynamic_cast<Collider2D*>(c.get())) { c->enabled = on; ++n; }
    return n;
}

/// Standing on something: a player controller's own answer when the object has one; otherwise a short
/// ray down from the bottom of its 3D collider (0.15 units), or a Rigidbody resting on terrain.
inline bool FrIsGrounded(GameObject* go) {
    if (!go) return false;
    if (auto* c = go->GetComponent<ThirdPersonController>(); c && c->enabled) return c->IsGrounded();
    if (auto* c = go->GetComponent<FirstPersonController>(); c && c->enabled) return c->IsGrounded();
    if (auto* c = go->GetComponent<CharacterController2D>(); c && c->enabled) return c->IsGrounded();
    if (auto* rb = go->GetComponent<Rigidbody3D>(); rb && rb->groundedOnTerrain) return true;
    Scene* s = go->scene();
    auto* col = go->GetComponent<Collider3D>();
    if (!s || !col) return false;
    Vec3 lo, hi;
    col->WorldAABB(lo, hi);
    const Vec3 from{(lo.x + hi.x) * 0.5f, lo.y + 0.05f, (lo.z + hi.z) * 0.5f};
    return s->physics3D().Raycast(*s, from, Vec3{0, -1, 0}, 0.2f, go).hit;
}

/// This object's debug line renderer (the one Draw Debug Line uses), set up for a debug shape.
inline LineRenderer* FrDebugLine(GameObject* go) {
    auto* line = go->GetComponent<LineRenderer>();
    if (!line) line = go->AddComponent<LineRenderer>();
    line->useWorldSpace = true;
    line->loop = false;
    line->width = 0.05f;
    line->color = Color{1.0f, 0.85f, 0.2f, 1.0f};
    return line;
}
/// A wire sphere as one line: the circles round X-Y, X-Z and Y-Z, joined where they meet so no
/// line crosses the inside. `seg` points per circle.
inline std::vector<Vec3> FrSpherePoints(const Vec3& c, float r, int seg = 24) {
    std::vector<Vec3> p;
    r = std::fabs(r);
    const float k = 6.2831853f / (float)seg;
    for (int i = 0; i <= seg; ++i) p.push_back(c + Vec3{r * std::cos(k * i), r * std::sin(k * i), 0.0f});      // X-Y from +X
    for (int i = 1; i <= seg; ++i) p.push_back(c + Vec3{r * std::cos(k * i), 0.0f, r * std::sin(k * i)});      // X-Z back to +X
    for (int i = 1; i <= seg / 4; ++i) p.push_back(c + Vec3{r * std::cos(k * i), 0.0f, r * std::sin(k * i)});  // on to +Z
    for (int i = 1; i <= seg; ++i) p.push_back(c + Vec3{0.0f, r * std::sin(k * i), r * std::cos(k * i)});      // Y-Z from +Z
    return p;
}
/// A wire box as one line through all 12 edges (some are drawn twice).
inline std::vector<Vec3> FrBoxPoints(const Vec3& c, const Vec3& size) {
    const Vec3 h{std::fabs(size.x) * 0.5f, std::fabs(size.y) * 0.5f, std::fabs(size.z) * 0.5f};
    auto at = [&](float sx, float sy, float sz) { return c + Vec3{sx * h.x, sy * h.y, sz * h.z}; };
    const Vec3 b0 = at(-1, -1, -1), b1 = at(1, -1, -1), b2 = at(1, -1, 1), b3 = at(-1, -1, 1);
    const Vec3 t0 = at(-1, 1, -1), t1 = at(1, 1, -1), t2 = at(1, 1, 1), t3 = at(-1, 1, 1);
    return {b0, b1, b2, b3, b0, t0, t1, t2, t3, t0, t1, b1, b2, t2, t3, b3};
}

/// The on-screen list Watch Value writes to: an object named "Watch Values" with a screen text,
/// made on first use. One line per watched name, "name: value", updated in place.
inline void FrWatch(Scene* s, const std::string& name, const std::string& value) {
    if (!s) return;
    GameObject* go = s->Find("Watch Values");
    if (!go) go = s->CreateGameObject("Watch Values");
    auto* t = go->GetComponent<TextRenderer>();
    if (!t) {
        t = go->AddComponent<TextRenderer>();
        t->text.clear();
        t->screenSpace = true;
        t->screenPos = {24, 120};
        t->pixelSize = 2.0f;
        t->size = {700, 400};
        t->color = Color{1.0f, 0.95f, 0.4f, 1.0f};
    }
    const std::string key = (name.empty() ? std::string("value") : name) + ": ";
    std::vector<std::string> lines;
    std::size_t from = 0;
    while (from <= t->text.size() && !t->text.empty()) {
        const std::size_t nl = t->text.find('\n', from);
        lines.push_back(t->text.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    bool found = false;
    for (auto& l : lines)
        if (l.rfind(key, 0) == 0) { l = key + value; found = true; }
    if (!found) lines.push_back(key + value);
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
    t->text = out;
}

} // namespace okay::vs
