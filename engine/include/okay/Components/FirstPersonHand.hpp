#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHand — a Minecraft-style first-person hand. It HIDES your body in
// first person and draws ONLY a forearm + fist in the lower corner of the view,
// coloured to match your Character (skin / sleeve / glove) so it clearly reads as
// YOUR arm. Click to swing it (a quick punch arc); it also triggers the real
// Character::Punch() so third-person and networked viewers see the swing too.
//
// The viewmodel is built at runtime and parented to the first-person camera, so
// it follows the view and never shows up as a stray object in the editor Scene
// view. Put this on the player or its FPS camera; weapons/held items can be
// layered on later. Tunable hand position, angle, size and swing speed.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Game.hpp"
#include <cmath>

namespace okay {

class FirstPersonHand : public Behaviour {
public:
    int   attackButton = 0;       ///< mouse button that swings (0 = left)
    bool  holdToSwing  = true;    ///< keep swinging while held (Minecraft mining)
    float swingDuration = 0.25f;  ///< seconds per swing
    bool  leftHanded   = false;   ///< put the hand on the left instead of the right
    bool  bob          = true;    ///< gentle idle sway

    // ---- Viewmodel placement (camera-local; tweak live in the inspector) ----
    float armScale = 1.0f;        ///< overall hand size
    float posX = 0.34f;           ///< right (+) / left (-) offset
    float posY = -0.32f;          ///< down (-) / up (+) offset
    float posZ = -0.55f;          ///< forward is negative (into the screen; clears the near plane)
    float yaw  = -16.0f;          ///< angle the fist toward screen centre
    float pitch = 10.0f;          ///< tilt up/down
    float roll  = 10.0f;          ///< diagonal Minecraft cant

    /// Trigger a swing from script / other components.
    void Punch() { StartSwing(); if (Character* c = FindCharacter()) c->Punch(); }
    bool Swinging() const { return m_swing >= 0.0f && m_swing < 1.0f; }

    void Start() override { Build(); }

    void Update(float dt) override {
        if (!m_built) Build();
        if (Game::Paused()) return;
        m_t += dt;
        if (Swinging()) {
            m_swing += (swingDuration > 1e-3f ? dt / swingDuration : 1.0f);
            if (m_swing > 1.0f) m_swing = 1.0f;
        }
        bool fire = holdToSwing ? Input::GetMouseButton(attackButton)
                                : Input::GetMouseButtonDown(attackButton);
        if (fire && !Swinging()) {
            StartSwing();
            if (Character* c = FindCharacter()) c->Punch();   // body / other viewers swing too
        }
        Place();
    }

    void OnDestroy() override {
        if (m_hand) if (Scene* s = GetScene()) s->Destroy(m_hand);
        m_hand = nullptr; m_built = false;
    }

private:
    GameObject*   m_hand  = nullptr;
    Transform*    m_cam   = nullptr;
    bool          m_built = false;
    float         m_t     = 0.0f;
    float         m_swing = -1.0f;   ///< <0 = idle, else 0..1 swing progress

    void StartSwing() { if (!Swinging()) m_swing = 0.0f; }

    void Build() {
        if (m_built) return;
        Scene* s = GetScene();
        Transform* cam = FindCamera();
        if (!s || !cam) return;
        // First person: hide the body — we draw a hand instead of the whole character.
        if (auto* fpc = FindController()) { fpc->showBody = false; fpc->ApplyBodyVisibility(); }
        m_cam = cam;
        GameObject* h = s->CreateGameObject("FP Hand");
        auto* mr = h->AddComponent<MeshRenderer>();
        mr->mesh = BuildArmMesh(FindCharacter());
        mr->doubleSided = true;
        mr->groundShadow = false;     // a viewmodel shouldn't cast a blob on the ground
        h->transform->SetParent(cam, false);
        m_hand = h; m_built = true;
        Place();
    }

    void Place() {
        if (!m_hand || !m_hand->transform) return;
        float side = leftHanded ? -1.0f : 1.0f;
        float arc = Swinging() ? std::sin(m_swing * 3.14159265f) : 0.0f;
        Vec3 p{posX * side, posY, posZ};
        if (bob) { p.y += std::sin(m_t * 1.7f) * 0.006f; p.x += std::sin(m_t * 0.9f) * 0.004f * side; }
        p.z += -0.10f * arc;          // jab forward (into the screen) on a swing
        p.y += -0.05f * arc;
        m_hand->transform->localPosition = p;
        m_hand->transform->localRotation = Quat::Euler(pitch + 45.0f * arc, yaw * side, roll * side);
        m_hand->transform->localScale = Vec3{armScale, armScale, armScale};
    }

    // A forearm (sleeve-coloured) + fist (skin/glove-coloured) pointing into the
    // screen, so it reads as the character's own arm. Per-face colours; blocky.
    static Mesh BuildArmMesh(Character* c) {
        Color skin   = c ? c->skin : Color::FromBytes(220, 176, 150);
        Color sleeve = (c && c->shirtStyle == 2) ? c->shirt : skin;   // long sleeve covers the forearm
        if (c && c->hasJacket) sleeve = c->jacket;
        Color hand   = (c && c->hasGloves) ? c->gloves : skin;

        auto box = [](float w, float hgt, float d, Vec3 center) {
            Mesh m = Mesh::Cube(1.0f);
            for (Vec3& v : m.vertices) { v.x *= w; v.y *= hgt; v.z *= d; v += center; }
            return m;
        };
        Mesh forearm = box(0.13f, 0.13f, 0.42f, {0.0f, 0.0f, -0.16f});  // wrist near camera -> forward
        int  fTris   = forearm.TriangleCount();
        Mesh arm = forearm;
        arm.Combine(box(0.17f, 0.17f, 0.17f, {0.0f, 0.0f, -0.40f}));    // fist at the forward end
        arm.triColors.clear();
        for (int i = 0; i < arm.TriangleCount(); ++i) arm.triColors.push_back(i < fTris ? sleeve : hand);
        return arm;
    }

    Character* FindCharacter() const {
        for (Transform* t = transform; t; t = t->Parent())
            if (t->gameObject) if (auto* c = t->gameObject->GetComponent<Character>()) return c;
        return nullptr;
    }
    FirstPersonController* FindController() const {
        for (Transform* t = transform; t; t = t->Parent())
            if (t->gameObject) if (auto* c = t->gameObject->GetComponent<FirstPersonController>()) return c;
        return nullptr;
    }
    // The first-person camera: search the whole player rig (from the topmost ancestor).
    Transform* FindCamera() const {
        Transform* root = transform;
        while (root && root->Parent()) root = root->Parent();
        return FindCameraIn(root ? root : transform);
    }
    static Transform* FindCameraIn(Transform* t) {
        if (!t) return nullptr;
        if (t->gameObject && t->gameObject->GetComponent<Camera>()) return t;
        for (Transform* c : t->Children()) if (Transform* r = FindCameraIn(c)) return r;
        return nullptr;
    }
};

} // namespace okay
