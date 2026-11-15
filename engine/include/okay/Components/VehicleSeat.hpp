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
//   - Saved: the fields below. Not saved: who sits in it (runtime only, like Seat).
//   - Multiplayer: nothing is sent over the network. Seat state is not replicated.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Net/PlayerInput.hpp"
#include "okay/Input/Input.hpp"
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
    char  enterKey = 'e';                ///< get in and get out
    bool  occupied = false;              ///< runtime only
    bool  playerInRange = false;         ///< runtime only

    GameObject* Occupant() const { return m_who; }
    /// The vehicle this seat belongs to: this object or the nearest parent with one.
    VehicleController* Vehicle() const {
        return gameObject ? gameObject->GetComponentInParent<VehicleController>(true) : nullptr;
    }
    Vec3 SitWorld() const { return transform ? transform->TransformPoint(sitPoint) : Vec3{}; }
    Vec3 ExitWorld() const { return transform ? transform->TransformPoint(exitPoint) : Vec3{}; }

    /// Get `who` into this seat. Returns false (and changes nothing) if the seat or the
    /// driver's place is taken, the person is already seated somewhere, or it is a driver
    /// seat with no vehicle. Does not check distance: the use key does that.
    bool Enter(GameObject* who) {
        if (!who || who->IsDestroyed() || !who->transform || !transform || !gameObject) return false;
        if (m_who) return false;
        if (FindSeatOf(who)) return false;
        VehicleController* v = Vehicle();
        if (driver) {
            if (!v) return false;
            if (v->Driver()) return false;                       // one driver per vehicle
        }
        m_who = who;
        occupied = true;
        m_gravityOwned = false;
        m_off.clear(); m_offColliders.clear();
        for (Behaviour* b : who->GetComponents<Behaviour>()) {   // player controllers: off while seated
            if (!dynamic_cast<IPlayerSim*>(b) || !b->enabled) continue;
            b->enabled = false;
            m_off.push_back(b);
        }
        for (Collider3D* c : who->GetComponents<Collider3D>()) { // the body must not shove the car
            if (!c->enabled) continue;
            c->enabled = false;
            m_offColliders.push_back(c);
        }
        if (auto* rb = who->GetComponent<Rigidbody3D>()) {
            rb->velocity = Vec3{0, 0, 0};
            m_savedGravity = rb->gravityScale;
            rb->gravityScale = 0.0f;
            m_gravityOwned = true;
        }
        if (driver && v) v->SetDriver(who);
        Carry();
        return true;
    }

    /// Put the occupant back on their feet at the exit point. No-op when empty.
    void Exit() {
        GameObject* who = m_who;
        if (!who) return;
        const bool alive = !who->IsDestroyed() && who->transform;
        Vec3 where = alive && transform ? PickExit(who) : Vec3{};
        ReleaseState(who, alive);
        if (alive && transform) who->transform->SetPosition(where);
    }

    void Awake() override { NoteSeat(); }
    void Start() override { NoteSeat(); }

    void Update(float /*dt*/) override {
        NoteSeat();
        if (m_who && (m_who->IsDestroyed() || !m_who->transform)) { Forget(); }
        if (m_who) {
            playerInRange = false;
            if (Input::GetKeyDown(enterKey) && IsLocallyControlled(m_who)) Exit();
            return;
        }
        playerInRange = false;
        if (!gameObject || !gameObject->scene() || !transform) return;
        GameObject* who = nullptr;
        float best = useRadius;
        const Vec3 sit = SitWorld();
        for (const auto& up : gameObject->scene()->Objects()) {
            if (!up || !up->active || !up->transform || up.get() == gameObject) continue;
            if (!IsLocallyControlled(up.get()) || !up->GetComponent<Character>()) continue;
            Vec3 d = up->transform->Position() - sit;
            d.y = 0.0f;
            const float dist = d.Magnitude();
            if (dist < best) { best = dist; who = up.get(); }
        }
        if (!who) return;
        VehicleController* v = Vehicle();
        if (driver && v && v->Driver()) return;          // taken: no prompt, no entry
        playerInRange = true;
        if (Input::GetKeyDown(enterKey)) Enter(who);
    }

    void LateUpdate(float /*dt*/) override {
        if (m_who && !m_who->IsDestroyed() && m_who->transform) Carry();
    }

    void OnDisable() override { Exit(); }
    void OnDestroy() override { Exit(); }

    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        if (m_who && dying.count(m_who)) Forget();        // the person is going away: free the seat
    }

private:
    void NoteSeat() { if (driver) if (VehicleController* v = Vehicle()) v->NoteDriverSeat(); }

    // Another seat that already holds `who`, if any.
    VehicleSeat* FindSeatOf(GameObject* who) const {
        if (!gameObject || !gameObject->scene()) return nullptr;
        for (const auto& up : gameObject->scene()->Objects()) {
            if (!up) continue;
            for (VehicleSeat* s : up->GetComponents<VehicleSeat>())
                if (s->m_who == who) return s;
        }
        return nullptr;
    }

    void Carry() {
        if (!m_who || !m_who->transform || !transform) return;
        m_who->transform->SetPosition(SitWorld());
        m_who->transform->SetRotation(transform->Rotation());
        if (auto* rb = m_who->GetComponent<Rigidbody3D>()) rb->velocity = Vec3{0, 0, 0};
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

    // Give the body back (controllers, colliders, gravity) and free the seat.
    void ReleaseState(GameObject* who, bool alive) {
        if (alive) {
            const auto ctrl = who->GetComponents<Behaviour>();
            for (Behaviour* b : m_off)
                for (Behaviour* c : ctrl) if (c == b) { b->enabled = true; break; }
            const auto cols = who->GetComponents<Collider3D>();
            for (Collider3D* b : m_offColliders)
                for (Collider3D* c : cols) if (c == b) { b->enabled = true; break; }
            if (m_gravityOwned)
                if (auto* rb = who->GetComponent<Rigidbody3D>()) {
                    rb->gravityScale = m_savedGravity;
                    rb->velocity = Vec3{0, 0, 0};
                }
        }
        Forget();
    }

    // Free the seat without touching the (maybe gone) person.
    void Forget() {
        if (driver)
            if (VehicleController* v = Vehicle())
                if (v->Driver() == m_who) v->SetDriver(nullptr);
        m_who = nullptr;
        occupied = false;
        m_off.clear(); m_offColliders.clear();
        m_gravityOwned = false;
    }

    GameObject* m_who = nullptr;
    std::vector<Behaviour*> m_off;
    std::vector<Collider3D*> m_offColliders;
    float m_savedGravity = 1.0f;
    bool  m_gravityOwned = false;
};

} // namespace okay
