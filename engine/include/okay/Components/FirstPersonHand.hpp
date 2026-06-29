#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHand — a Minecraft-style first-person hand. It HIDES your character
// body in first person and draws ONLY a short forearm + fist in the lower corner,
// coloured to match your Character (skin / sleeve / glove) so it reads as your own
// hand. Because the body is hidden there is never a second arm, and you never see
// your torso/legs while walking. Click to swing it (this also fires the real
// Character::Punch() so third-person + networked players see the hit).
//
// The viewmodel is built at runtime and parented to the first-person camera, on a
// reserved layer the editor Scene view hides, so it never shows as a stray object
// while editing. Put it on the player or its FPS camera.
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
    float swingDuration = 0.22f;  ///< seconds per swing

    /// Whether the player is holding a weapon/item. Empty-handed the hand rests low
    /// (out of the way, Minecraft-style); set true (e.g. when a weapon is equipped)
    /// to bring it up into view. Defaults to empty.
    bool  holdingItem = false;
    void  SetHolding(bool h) { holdingItem = h; }

    // ---- Viewmodel placement (camera-local; tweak live in the inspector) ----
    float armScale  = 1.2f;       ///< overall hand size
    float armLength = 0.18f;      ///< forearm length — small = hand closer to the camera
    float posX = 0.30f;           ///< right (+) / left (-) offset
    float posY = -0.34f;          ///< down (-) / up (+) offset
    float posZ = -0.42f;          ///< forward is negative (into the screen). Closer = nearer the eye.
    float yaw  = -14.0f;
    float pitch = -52.0f;         ///< tip the upward forearm forward into the view
    float roll  = -20.0f;

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
            if (Character* c = FindCharacter()) c->Punch();
        }
        Place();
    }

    void OnDestroy() override {
        if (m_hand) if (Scene* s = GetScene()) s->Destroy(m_hand);
        m_hand = nullptr; m_built = false;
    }

private:
    GameObject* m_hand  = nullptr;
    bool        m_built = false;
    float       m_t     = 0.0f;
    float       m_swing = -1.0f;
    float       m_raise = 0.0f;   ///< 0 = lowered (empty), 1 = raised (holding); eased

    void StartSwing() { if (!Swinging()) m_swing = 0.0f; }

    void Build() {
        if (m_built) return;
        Scene* s = GetScene();
        Transform* cam = FindCamera();
        if (!s || !cam) return;
        // First person: hide the body — we draw only the hand.
        if (auto* fpc = FindController()) { fpc->showBody = false; fpc->ApplyBodyVisibility(); }
        GameObject* h = s->CreateGameObject("FP Hand");
        h->layer = 31;   // reserved viewmodel layer: cameras show it, the editor Scene view hides it
        auto* mr = h->AddComponent<MeshRenderer>();
        mr->mesh = BuildArmMesh(FindCharacter());
        mr->doubleSided = true;
        mr->groundShadow = false;
        h->transform->SetParent(cam, false);
        m_hand = h; m_built = true;
        Place();
    }

    void Place() {
        if (!m_hand || !m_hand->transform) return;
        float arc = Swinging() ? std::sin(m_swing * 3.14159265f) : 0.0f;
        // Ease the hand between its "up" (holding) pose and a low resting pose
        // (empty-handed) so equipping/holstering an item is a smooth raise/lower.
        float target = holdingItem ? 1.0f : 0.0f;
        m_raise += (target - m_raise) * 0.2f;
        float drop = (1.0f - m_raise);                 // 0 = up, 1 = lowered (rests low, still peeking)
        Vec3 p{posX, posY - 0.17f * drop, posZ + 0.04f * drop};
        p.y += std::sin(m_t * 1.7f) * 0.005f;          // gentle idle sway
        p.z += -0.08f * arc;                           // jab forward on a swing
        p.y += -0.05f * arc;
        m_hand->transform->localPosition = p;
        m_hand->transform->localRotation = Quat::Euler(pitch + 42.0f * arc + 30.0f * drop, yaw, roll);
        m_hand->transform->localScale = Vec3{armScale, armScale, armScale};
    }

    // A short forearm (sleeve-coloured) + fist (skin/glove-coloured), built rising
    // along +Y so that — tipped forward and placed low — it reads as your hand held
    // up close. Per-face colours from the Character; blocky.
    Mesh BuildArmMesh(Character* c) const {
        Color skin   = c ? c->skin : Color::FromBytes(220, 176, 150);
        Color sleeve = (c && c->shirtStyle == 2) ? c->shirt : skin;
        if (c && c->hasJacket) sleeve = c->jacket;
        Color hand   = (c && c->hasGloves) ? c->gloves : skin;

        auto box = [](float w, float hgt, float d, Vec3 center) {
            Mesh m = Mesh::Cube(1.0f);
            for (Vec3& v : m.vertices) { v.x *= w; v.y *= hgt; v.z *= d; v += center; }
            return m;
        };
        float L = armLength < 0.04f ? 0.04f : armLength;
        Mesh forearm = box(0.14f, L, 0.14f, {0.0f, 0.0f, 0.0f});
        int  fTris   = forearm.TriangleCount();
        Mesh arm = forearm;
        arm.Combine(box(0.18f, 0.18f, 0.18f, {0.0f, L * 0.5f + 0.07f, 0.0f}));   // fist at the top
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
