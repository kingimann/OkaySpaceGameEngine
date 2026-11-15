#pragma once
// A seat in a vehicle. Put it on the vehicle (or on a child of it). A driver seat lets a
// player get in with the use key (default E): the player controller and colliders switch
// off, the body is carried at sitPoint, and the vehicle reads the driver's keys. Press the
// key again to get out at exitPoint. A passenger seat (driver = false) only carries the player.
//
// Rules:
//   - One driver per vehicle: a second Enter on a driver seat is refused. One seat per person.
//   - Exit tries exitPoint, then its mirror images, then the roof. The first spot with no
//     solid collider is used. If all are blocked the roof is used anyway.
//   - The driver leaving makes the vehicle brake to a stop (VehicleController::brakeForce).
//   - Destroying the driver, the seat or the vehicle never leaves a stale pointer.
//   - Who can board: a local player, meaning an object with a player controller (IPlayerSim:
//     first / third person, character controller 2D / 3D, top-down 2D). AI (NPCController) cannot.
//   - A dead occupant is put out. A person sitting in a chair Seat cannot board, and the other way round.
//   - Saved: the fields below. Not saved: who sits in it (runtime only, like Seat). A scene saved while
//     someone sits is written with the player's controller, colliders and gravity as they were before
//     they got in (the seat hands them back for the save and takes them again after).
//   - A car whose only seats are passenger seats (or that has none) keeps the old behaviour: it always
//     reads the keys. Only a driver seat that is switched on makes the car wait for a driver.
//   - Multiplayer: nothing is sent over the network. Seat state is not replicated.
//   - Sitting: the occupant plays the same chair sit clips as a chair Seat (HumanoidActions::BeginVehicleSit,
//     SitDown then Sit) and plays StandUp on exit. Only a body with the humanoid animator sits; any other body
//     is just carried upright. The body is turned to face the car's forward (humanoids face -Z, cars drive +Z).
//     sitPoint is where the body's FEET root goes: the floor under the seat, not the seat cushion (the sit
//     clip drops the hips about 0.48 m, so a cushion 0.45 m above sitPoint matches).
//   - Camera: if the scene's main camera is the occupant's own (the player's camera), it is taken over while
//     seated. Third person: a chase view behind and above the car (VehicleController camDistance, camHeight,
//     camLerp; it eases in from where the orbit was). First person: at the seated eyes, looking along the car.
//     On exit the camera gets its old local pose back and the player controller takes over again. A camera that
//     is not the occupant's is left alone (VehicleController followCamera still works for it).
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/Seat.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Net/PlayerInput.hpp"
#include "okay/Input/Input.hpp"
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

namespace okay {

class VehicleSeat : public Behaviour {
public:
    bool  driver = true;                 ///< true: sitting here drives the vehicle. false: passenger.
    Vec3  sitPoint{0.0f, 0.6f, 0.0f};    ///< local point the body is carried at
    Vec3  exitPoint{-1.8f, 0.0f, 0.0f};  ///< local point they step out to (left of the car)
    float useRadius = 2.0f;              ///< how close the player must be to get in (metres, flat)
    std::string prompt = "Drive";        ///< shown while a player is in range
    char  enterKey = 'e';                ///< get in and get out (raw key only: pad and input actions are not read)
    bool  occupied = false;              ///< runtime only
    bool  playerInRange = false;         ///< runtime only

    ~VehicleSeat() override {   // a scene torn down while someone sits never calls OnDestroy
        auto& occ = Occupied();
        for (size_t i = 0; i < occ.size(); ++i) if (occ[i] == this) { occ.erase(occ.begin() + (long)i); break; }
    }

    GameObject* Occupant() const { return m_who; }
    /// The car the camera is following (the vehicle's object, else the root of this seat) while this seat
    /// has taken over the occupant's camera; nullptr otherwise.
    GameObject* CameraTarget() const { return m_cam ? CarRoot()->gameObject : nullptr; }
    /// True while the occupant plays the sit animation (false for a body with no humanoid animator).
    bool PoseApplied() const { return m_posed; }
    /// The vehicle this seat belongs to: this object or the nearest parent with one.
    VehicleController* Vehicle() const {
        return gameObject ? gameObject->GetComponentInParent<VehicleController>(true) : nullptr;
    }
    Vec3 SitWorld() const { return transform ? transform->TransformPoint(sitPoint) : Vec3{}; }
    Vec3 ExitWorld() const { return transform ? transform->TransformPoint(exitPoint) : Vec3{}; }

    /// True while `who` sits in any VehicleSeat.
    static bool IsSeated(const GameObject* who) {
        for (VehicleSeat* s : Occupied()) if (s->m_who == who) return true;
        return false;
    }

    /// Get `who` into this seat. Returns false (and changes nothing) if the seat or the
    /// driver's place is taken, the person is already seated anywhere (here or on a chair Seat),
    /// is dead, is this seat's own object or one of its parents, or it is a driver seat with no
    /// vehicle. Does not check distance or kind: the use key does that.
    bool Enter(GameObject* who) {
        if (!who || who->IsDestroyed() || !who->transform || !transform || !gameObject) return false;
        if (m_who) return false;
        if (IsSeated(who) || SittingOnChair(who)) return false;
        if (HumanoidActions::IsDead(who)) return false;
        for (const Transform* t = transform; t; t = t->Parent())   // the car cannot carry its own parent
            if (t->gameObject == who) return false;
        VehicleController* v = Vehicle();
        if (driver) {
            if (!v) return false;
            if (v->Driver()) return false;                       // one driver per vehicle
        }
        m_who = who;
        occupied = true;
        m_off.clear(); m_offColliders.clear();
        for (Behaviour* b : who->GetComponents<Behaviour>())     // player controllers: off while seated
            if (dynamic_cast<IPlayerSim*>(b) && b->enabled) m_off.push_back(b);
        for (Collider3D* c : who->GetComponentsInChildren<Collider3D>(true))   // body and hitboxes: off
            if (c->enabled) m_offColliders.push_back(c);
        m_gravityOwned = false;
        if (auto* rb = who->GetComponent<Rigidbody3D>()) {
            rb->velocity = Vec3{0, 0, 0};
            m_savedGravity = rb->gravityScale;
            m_gravityOwned = true;
        }
        ApplyState(who, true);
        Occupied().push_back(this);
        if (driver && v) v->SetDriver(who);
        m_posed = HumanoidActions::BeginVehicleSit(who);
        TakeCamera(who);
        Carry();
        return true;
    }

    /// Put the occupant back on their feet at the exit point, upright. No-op when empty.
    void Exit() {
        GameObject* who = m_who;
        if (!who) return;
        const bool alive = !who->IsDestroyed() && who->transform;
        GiveCameraBack();
        if (alive && m_posed) HumanoidActions::RequestStandUp(who);
        Vec3 where = alive && transform ? PickExit(who) : Vec3{};
        ReleaseState(who, alive);
        if (alive && transform) {
            const Vec3 f = who->transform->Forward();   // the body faces -Z, so this is the car's rear
            const float yaw = std::atan2(f.x, f.z) * 57.2957795f;
            who->transform->SetRotation(Quat::Euler(0.0f, yaw, 0.0f));   // no roll or pitch from the car
            who->transform->SetPosition(where);
        }
    }

    // ---- Saving: hand the occupant's own state back while a scene is written ----
    // (SceneSerializer does this around every save, so a save, copy, undo snapshot or autosave
    // made while someone sits never writes a switched-off controller or zero gravity.)
    static void SuspendAllForSave() {
        if (s_saveDepth()++ == 0) for (VehicleSeat* s : Occupied()) s->SuspendForSave();
    }
    static void ResumeAllAfterSave() {
        if (--s_saveDepth() == 0) for (VehicleSeat* s : Occupied()) s->ResumeAfterSave();
    }

    void Awake() override { Sync(); }
    void Start() override { Sync(); }
    void OnEnable() override { Sync(); }

    void Update(float /*dt*/) override {
        Sync();
        if (m_who && (m_who->IsDestroyed() || !m_who->transform)) Forget();
        if (m_who && HumanoidActions::IsDead(m_who)) Exit();   // a dead body is not at the wheel
        const bool down = Input::GetKeyDown(enterKey);
        if (!down) s_justExited() = nullptr;
        playerInRange = false;
        if (m_who) {
            if (down && IsLocallyControlled(m_who)) { s_justExited() = m_who; Exit(); }
            return;
        }
        if (!gameObject || !gameObject->scene() || !transform) return;
        GameObject* who = nullptr;
        float best = useRadius;
        const Vec3 sit = SitWorld();
        for (const auto& up : gameObject->scene()->Objects()) {
            if (!up || !up->active || !up->transform || up.get() == gameObject) continue;
            if (!IsLocallyControlled(up.get()) || !FindPlayerSim(up.get())) continue;   // players only, no AI
            if (IsSeated(up.get())) continue;
            Vec3 d = up->transform->Position() - sit;
            d.y = 0.0f;
            const float dist = d.Magnitude();
            if (dist < best) { best = dist; who = up.get(); }
        }
        if (!who) return;
        VehicleController* v = Vehicle();
        if (driver && v && v->Driver()) return;          // taken: no prompt, no entry
        playerInRange = true;
        // Another seat of this car may have let the same person out on this very key press.
        if (down && who != s_justExited()) Enter(who);
    }

    void LateUpdate(float dt) override {
        if (m_who && !m_who->IsDestroyed() && m_who->transform) { Carry(); RideCamera(dt); }
    }

    void OnDisable() override { Exit(); Unregister(); }
    void OnDestroy() override { Exit(); Unregister(); }

    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        if (m_who && dying.count(m_who)) Forget();        // the person is going away: free the seat
    }

private:
    static std::vector<VehicleSeat*>& Occupied() { static std::vector<VehicleSeat*> v; return v; }
    static int& s_saveDepth() { static int d = 0; return d; }
    static GameObject*& s_justExited() { static GameObject* g = nullptr; return g; }

    // Keep the car's count of live driver seats right: a seat counts while it is a driver seat,
    // switched on, and on a vehicle. Removing it or turning `driver` off stops it counting.
    void Sync() {
        VehicleController* v = Vehicle();
        const bool want = driver && enabled && v;
        if (m_reg && (!want || m_reg != v)) Unregister();
        if (want && !m_reg) { v->AddDriverSeat(); m_reg = v; }
    }
    void Unregister() {
        if (m_reg && Vehicle() == m_reg) m_reg->RemoveDriverSeat();   // m_reg is only compared unless it is still live
        m_reg = nullptr;
    }

    static bool SittingOnChair(GameObject* who) {
        if (HumanoidActions::IsSitting(who)) return true;
        if (who->scene())
            for (const auto& up : who->scene()->Objects()) {
                Seat* c = up ? up->GetComponent<Seat>() : nullptr;
                if (c && c->Occupant() == who) return true;
            }
        return false;
    }

    void Carry() {
        if (!m_who || !m_who->transform || !transform) return;
        m_who->transform->SetPosition(SitWorld());
        m_who->transform->SetRotation((transform->Rotation() * Quat::Euler(0.0f, 180.0f, 0.0f)).Normalized());   // body faces -Z
        if (auto* rb = m_who->GetComponent<Rigidbody3D>()) rb->velocity = Vec3{0, 0, 0};
    }

    // ---- Ride camera ----
    Transform* CarRoot() const {
        VehicleController* v = Vehicle();
        if (v && v->transform) return v->transform;
        Transform* t = transform;
        while (t && t->Parent()) t = t->Parent();
        return t;
    }
    // Take over the occupant's own main camera (a camera on the occupant or one of its children).
    void TakeCamera(GameObject* who) {
        m_cam = nullptr;
        Scene* sc = who->scene();
        if (!sc || !sc->mainCamera || !sc->mainCamera->gameObject || !sc->mainCamera->gameObject->transform) return;
        Transform* ct = sc->mainCamera->gameObject->transform;
        bool mine = false;
        for (Transform* t = ct; t; t = t->Parent()) if (t->gameObject == who) { mine = true; break; }
        if (!mine) return;
        m_cam = sc->mainCamera;
        m_camPos = ct->localPosition;
        m_camRot = ct->localRotation;
        auto* pc = PlayerController::Of(who);
        m_camFirst = pc && pc->CurrentView() == 0;
        if (VehicleController* v = Vehicle()) v->SetCameraDrivenBySeat(true);
    }
    void RestoreCameraPose() {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (m_cam && sc && sc->mainCamera == m_cam && m_cam->gameObject && m_cam->gameObject->transform) {   // m_cam is only compared unless it is still the live main camera
            m_cam->gameObject->transform->localPosition = m_camPos;
            m_cam->gameObject->transform->localRotation = m_camRot;
        }
    }
    void GiveCameraBack() {
        RestoreCameraPose();
        if (m_cam) if (VehicleController* v = Vehicle()) v->SetCameraDrivenBySeat(false);
        m_cam = nullptr;
    }
    void RideCamera(float dt) {
        if (!m_cam) return;
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc || sc->mainCamera != m_cam || !m_cam->gameObject || !m_cam->gameObject->transform || !transform) {
            if (VehicleController* v = Vehicle()) v->SetCameraDrivenBySeat(false);
            m_cam = nullptr;   // the camera was replaced or removed: let go
            return;
        }
        Transform* ct = m_cam->gameObject->transform;
        Transform* car = CarRoot();
        if (!car) return;
        const Quat rot = car->Rotation();
        Vec3 fp = rot * Vec3::Forward; fp.y = 0.0f;
        const float fl = fp.Magnitude();
        fp = fl > 1e-5f ? fp * (1.0f / fl) : Vec3{0, 0, 1};
        if (m_camFirst) {   // eyes: a little above and ahead of the sitting hips, looking along the car
            ct->SetPosition(SitWorld() + rot * Vec3{0.0f, 1.2f, 0.12f});
            ct->SetRotation((rot * Quat::Euler(0.0f, 180.0f, 0.0f)).Normalized());   // camera looks down its -Z
            return;
        }
        VehicleController* v = Vehicle();
        const float dist = v ? v->camDistance : 6.5f, high = v ? v->camHeight : 2.6f, lerp = v ? v->camLerp : 5.0f;
        const Vec3 carPos = car->Position();
        const Vec3 want = carPos - fp * dist + Vec3{0.0f, high, 0.0f};
        float t = lerp > 0.0f ? 1.0f - std::exp(-lerp * dt) : 1.0f;
        if (t > 1.0f) t = 1.0f; else if (t < 0.0f) t = 0.0f;
        const Vec3 np = ct->Position() + (want - ct->Position()) * t;
        ct->SetPosition(np);
        const Vec3 look = (carPos + Vec3{0.0f, high * 0.3f, 0.0f} + fp * (dist * 0.35f)) - np;
        if (look.SqrMagnitude() > 1e-6f)
            ct->SetRotation(Quat::Slerp(ct->Rotation(), Quat::LookRotation(look * -1.0f), t));
    }

    // True when a standing person at `feet` would be inside something solid.
    bool Blocked(GameObject* who, const Vec3& feet) const {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc) return false;
        return !sc->physics3D().OverlapSphere(*sc, feet + Vec3{0, 0.9f, 0}, 0.4f,
                                              Physics3D::kAllLayers, false, who).empty();
    }

    Vec3 PickExit(GameObject* who) const {
        const Vec3 e = exitPoint;
        const Vec3 tries[4] = {e, Vec3{-e.x, e.y, e.z}, Vec3{e.x, e.y, -e.z}, Vec3{-e.x, e.y, -e.z}};
        for (const Vec3& t : tries) {
            Vec3 w = transform->TransformPoint(t);
            if (!Blocked(who, w)) return w;
        }
        return SitWorld() + Vec3{0.0f, 2.5f, 0.0f};          // all blocked: roof
    }

    // Switch the occupant's controllers, colliders and gravity to the seated (true) or standing
    // (false) state. Only things that are still on the object are touched.
    void ApplyState(GameObject* who, bool seated) {
        const auto ctrl = who->GetComponents<Behaviour>();
        for (Behaviour* b : m_off)
            for (Behaviour* c : ctrl) if (c == b) { b->enabled = !seated; break; }
        const auto cols = who->GetComponentsInChildren<Collider3D>(true);
        for (Collider3D* b : m_offColliders)
            for (Collider3D* c : cols) if (c == b) { b->enabled = !seated; break; }
        if (m_gravityOwned)
            if (auto* rb = who->GetComponent<Rigidbody3D>()) {
                rb->gravityScale = seated ? 0.0f : m_savedGravity;
                if (!seated) rb->velocity = Vec3{0, 0, 0};
            }
    }

    void SuspendForSave() {
        if (m_suspended || !m_who || m_who->IsDestroyed()) return;
        ApplyState(m_who, false);
        RestoreCameraPose();                        // the saved camera is the player's own, not the chase pose
        m_suspended = true;
    }
    void ResumeAfterSave() {
        if (!m_suspended) return;
        m_suspended = false;
        if (m_who && !m_who->IsDestroyed()) ApplyState(m_who, true);
    }

    // Give the body back (controllers, colliders, gravity) and free the seat.
    void ReleaseState(GameObject* who, bool alive) {
        if (alive) ApplyState(who, false);
        Forget();
    }

    // Free the seat without touching the (maybe gone) person.
    void Forget() {
        if (driver)
            if (VehicleController* v = Vehicle())
                if (v->Driver() == m_who) v->SetDriver(nullptr);
        auto& occ = Occupied();
        for (size_t i = 0; i < occ.size(); ++i) if (occ[i] == this) { occ.erase(occ.begin() + (long)i); break; }
        if (m_who && !m_who->IsDestroyed()) GiveCameraBack();
        else if (m_cam) { if (VehicleController* v = Vehicle()) v->SetCameraDrivenBySeat(false); }
        m_cam = nullptr;
        m_posed = false;
        m_who = nullptr;
        occupied = false;
        m_suspended = false;
        m_off.clear(); m_offColliders.clear();
        m_gravityOwned = false;
    }

    GameObject* m_who = nullptr;
    VehicleController* m_reg = nullptr;     // the car this seat is counted on (compared, not followed)
    std::vector<Behaviour*> m_off;
    std::vector<Collider3D*> m_offColliders;
    float m_savedGravity = 1.0f;
    bool  m_gravityOwned = false;
    bool  m_suspended = false;
    bool  m_posed = false;                  // the sit animation was started
    Camera* m_cam = nullptr;                // the occupant's camera while this seat drives it (compared, not followed)
    Vec3  m_camPos{0, 0, 0};                // its local pose before boarding
    Quat  m_camRot = Quat::Identity;
    bool  m_camFirst = false;               // first-person view: eyes at the seat
};

} // namespace okay
