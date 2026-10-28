#pragma once
// A throwaway character the First Person Hands window renders twice: once from
// the owner's camera, once from in front. Edit mode and Play both drive it
// from the component being inspected. It is not part of the user's scene.
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/FirstPersonHands.hpp"
#include "okay/Components/HoldableObject.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Scene/Scene.hpp"
#include <cstdint>
#include <vector>

namespace okay {

class FpHandsPreview {
public:
    int Example() const { return m_which; }
    FirstPersonHands* Hands() const { return m_hands; }
    HoldableObject* ExampleItem() const {
        GameObject* g = m_which == 1 ? m_crate : m_block;
        return g ? g->GetComponent<HoldableObject>() : nullptr;
    }

    void CopyFrom(const FirstPersonHands& src) {
        Ensure();
        if (m_hands) m_hands->CopySettingsFrom(src);
    }

    void SetExample(int which) {
        Ensure();
        if (which < 0) which = 0;
        if (which > 2) which = 2;
        const bool holdingRight = m_hold && ((which == 2 && !m_hold->IsHolding())
            || (which != 2 && m_hold->IsHolding() && m_hold->Held() == (which == 1 ? m_crate : m_block)));
        if (m_ready && which == m_which && holdingRight) return;
        m_which = which;
        if (!m_hold) return;
        m_hold->Drop();
        if (m_block) m_block->active = true;
        if (m_crate) m_crate->active = true;
        if (which != 2) {
            GameObject* item = which == 1 ? m_crate : m_block;
            if (item) m_hold->Pickup(item);
        }
        if (m_block && which != 0) m_block->active = false;
        if (m_crate && which != 1) m_crate->active = false;
        m_warm = 0;
    }

    void Step(int frames) {
        Ensure();
        if (frames < 1) frames = 1;
        for (int i = 0; i < frames; ++i) {
            if (m_cam) {
                m_cam->ignoreObject = m_player;
                m_cam->nearClip = 0.04f;
            }
            if (m_ch) {
                m_ch->separateParts = true;
                m_ch->firstPersonArm = true;
                m_ch->fpArmBob = false;
                m_ch->fpSteady = true;
                m_ch->ApplyFirstPersonHandDrive(false);
                m_ch->fpPitch = 0.0f;
                if (m_ch->PartsBuilt()) m_ch->ApplyOwnerVisibility(true);
            }
            m_scene.Update(1.0f / 60.0f);
            if (m_ch && m_ch->PartsBuilt()) m_ch->ApplyOwnerVisibility(true);
            ++m_warm;
        }
    }

    /// ABGR8888, sky filled in where nothing drew. `firstPerson` is the owner's camera.
    std::vector<std::uint32_t> Render(bool firstPerson, int w, int h) {
        Ensure();
        if (w < 8) w = 8;
        if (h < 8) h = 8;
        if (m_cam) m_cam->viewportAspect = (float)w / (float)h;
        if (m_ch && m_ch->PartsBuilt()) m_ch->RefreshOwnerArm();
        ApplySceneLight(m_scene);
        const float aspect = (float)w / (float)h;
        Mat4 vp;
        Vec3 eye;
        const GameObject* ignore = nullptr;
        if (firstPerson && m_cam && m_cam->transform) {
            eye = m_cam->transform->Position();
            vp = m_cam->ProjectionMatrix(aspect) * m_cam->ViewMatrix();
            ignore = m_cam->ignoreObject;
        } else if (m_player && m_player->transform) {
            const Vec3 focus = m_player->transform->Position() + Vec3{0.0f, 1.15f, 0.0f};
            eye = focus + Vec3{0.95f, 0.42f, -1.85f};
            vp = Mat4::Perspective(32.0f, aspect, 0.05f, 40.0f) * Mat4::LookAt(eye, focus, Vec3::Up);
        }
        Raster work;
        std::vector<std::uint32_t> down;
        const std::uint32_t* px = RenderMeshesSS(work, down, m_scene, vp, eye, w, h, 1, ignore);
        std::vector<std::uint32_t> out(px, px + (std::size_t)w * h);
        const std::uint32_t sky = 0xFFC8AA8Cu;   // ABGR, a soft sky
        for (std::uint32_t& p : out)
            if ((p >> 24) == 0) p = sky;
        return out;
    }

private:
    void Ensure() {
        if (m_ready) return;
        m_scene.physicsEnabled = false;
        m_scene.runScripts = false;
        GameObject* light = m_scene.CreateGameObject("Light");
        light->AddComponent<Light>();
        light->transform->localRotation = Quat::Euler(50.0f, -30.0f, 0.0f);
        GameObject* ground = m_scene.CreateGameObject("Ground");
        ground->transform->localPosition = {0.0f, -0.5f, 0.0f};
        ground->transform->localScale = {24.0f, 1.0f, 24.0f};
        auto* gmr = ground->AddComponent<MeshRenderer>();
        gmr->mesh = Mesh::Cube();
        gmr->color = Color::FromBytes(118, 132, 148);

        m_player = m_scene.CreateGameObject("PreviewPlayer");
        m_ch = m_player->AddComponent<Character>();
        m_ch->Apply();
        m_ch->separateParts = true;
        m_hands = m_player->AddComponent<FirstPersonHands>();
        m_hands->showWhenEmpty = false;
        m_hold = m_player->AddComponent<ProceduralObjectHolding>();
        m_hold->transitionTime = 0.0f;

        GameObject* eye = m_scene.CreateGameObject("PreviewEye");
        eye->transform->SetParent(m_player->transform, false);
        eye->transform->localPosition = {0.0f, 1.62f, 0.0f};
        m_cam = eye->AddComponent<Camera>();
        m_cam->nearClip = 0.04f;
        m_cam->fieldOfView = 60.0f;

        m_block = MakeItem("Block", {0.14f, 0.14f, 0.14f}, Color::FromBytes(214, 122, 48), false);
        m_crate = MakeItem("Crate", {0.46f, 0.32f, 0.30f}, Color::FromBytes(176, 122, 64), true);
        m_scene.Start();
        m_ready = true;
        const int which = m_which;
        m_which = -1;
        SetExample(which);
    }

    GameObject* MakeItem(const char* name, Vec3 scale, Color color, bool two) {
        GameObject* g = m_scene.CreateGameObject(name);
        g->transform->localPosition = {0.3f, 1.0f, -0.6f};
        g->transform->localScale = scale;
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->color = color;
        auto* h = g->AddComponent<HoldableObject>();
        if (two) h->ApplyPreset(HoldableObject::Preset::CarryFront);
        else { h->ApplyPreset(HoldableObject::Preset::OneHandRight); h->weight = 0.4f; }
        return g;
    }

    Scene m_scene{"FpHandsPreview"};
    GameObject* m_player = nullptr;
    Character* m_ch = nullptr;
    Camera* m_cam = nullptr;
    FirstPersonHands* m_hands = nullptr;
    ProceduralObjectHolding* m_hold = nullptr;
    GameObject* m_block = nullptr;
    GameObject* m_crate = nullptr;
    int m_which = 0;
    int m_warm = 0;
    bool m_ready = false;
};

} // namespace okay
