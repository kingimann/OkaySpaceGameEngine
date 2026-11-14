#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/SceneTemplates.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace okay;

// Every template scene must keep a working camera: built, played for a few
// frames, and after a save/load round trip. No object that looks like a camera
// may be left without its Camera component.
static void CheckCameras(Scene& s, const char* tmpl, const char* stage) {
    int cams = 0, mains = 0;
    for (const auto& up : s.Objects()) {
        if (!up) continue;
        Camera* c = up->GetComponent<Camera>();
        if (c) { ++cams; if (c->main && up->active && c->enabled) ++mains; }
        if (!c && up->name.find("Camera") != std::string::npos) {
            std::printf("  %s/%s: '%s' has no Camera component\n", tmpl, stage, up->name.c_str());
            CHECK(false);
        }
    }
    if (cams == 0) std::printf("  %s/%s: no camera\n", tmpl, stage);
    CHECK(cams >= 1);
    if (mains != 1) std::printf("  %s/%s: %d main cameras\n", tmpl, stage, mains);
    CHECK(mains == 1);
}

int main() {
    RUN_SUITE("scene_cameras");
    struct T { const char* name; void (*build)(Scene&); };
    const T templates[] = {
        {"Platformer", Templates::Platformer}, {"Platformer3D", Templates::Platformer3D},
        {"ThirdPerson", Templates::ThirdPerson}, {"Humanoid", Templates::Humanoid},
        {"ThirdPersonShooter", Templates::ThirdPersonShooter}, {"PointAndClick", Templates::PointAndClick},
        {"FPS", Templates::FPS}, {"TopDown", Templates::TopDown}, {"Vehicle3D", Templates::Vehicle3D},
        {"Vehicle2D", Templates::Vehicle2D}, {"Multiplayer", Templates::Multiplayer},
        {"CoinCollector", Templates::CoinCollector}, {"MainMenu", Templates::MainMenu},
        {"Inventory", Templates::Inventory}, {"Snake", Templates::Snake},
        {"BrickBreaker", Templates::BrickBreaker},
        {"TerrainSandbox", Templates::TerrainSandbox}, {"VoxelSandbox", Templates::VoxelSandbox},
    };
    for (const T& t : templates) {
        std::string text;
        {
            Scene s; s.physicsEnabled = false;
            t.build(s);
            EnsureSceneCamera(s, SceneLooks2D(s));
            CheckCameras(s, t.name, "built");
            text = SceneSerializer::Serialize(s);
            s.Start();
            for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
            CheckCameras(s, t.name, "played");
            CHECK(s.mainCamera != nullptr);
        }
        Scene r; r.physicsEnabled = false;
        std::string err;
        const bool ok = SceneSerializer::Deserialize(r, text, &err);
        if (!ok) std::printf("  %s: reload failed: %s\n", t.name, err.c_str());
        CHECK(ok);
        CheckCameras(r, t.name, "reloaded");
        r.Start();
        for (int i = 0; i < 5; ++i) r.Update(1.0f / 60.0f);
        CheckCameras(r, t.name, "reloaded+played");
        CHECK(r.mainCamera != nullptr);
    }
    {   // Removing the only Camera component: it is put back on that object, not a
        // second "Main Camera" beside a camera object that renders nothing.
        Scene s; s.physicsEnabled = false;
        Templates::ThirdPerson(s);
        GameObject* camObj = nullptr;
        for (const auto& up : s.Objects()) if (up->GetComponent<Camera>()) camObj = up.get();
        CHECK(camObj != nullptr);
        const std::size_t before = s.Objects().size();
        camObj->RemoveComponent<Camera>();
        bool made = false, restored = false;
        Camera* c = EnsureSceneCamera(s, false, &made, &restored);
        CHECK(made && restored);
        CHECK(c && c->gameObject == camObj);
        CHECK(s.Objects().size() == before);
        CheckCameras(s, "ThirdPerson", "component removed");
    }
    {   // Switching cameras by turning objects on/off, and Depth between Main cameras.
        Scene s; s.physicsEnabled = false;
        GameObject* a = s.CreateGameObject("Cam A"); auto* ca = a->AddComponent<Camera>(); ca->main = true;
        GameObject* b = s.CreateGameObject("Cam B"); auto* cb = b->AddComponent<Camera>(); cb->main = false;
        s.Start(); s.Update(0.016f);
        CHECK(s.mainCamera == ca);
        a->active = false; s.Update(0.016f);
        CHECK(s.mainCamera == cb);                 // A off: B takes over
        a->active = true; s.Update(0.016f);
        CHECK(s.mainCamera == ca);                 // A is Main: back to A
        ca->enabled = false; s.Update(0.016f);
        CHECK(s.mainCamera == cb);                 // component disabled counts too
        ca->enabled = true;
        cb->main = true; cb->depth = 5.0f; s.Update(0.016f);
        CHECK(s.mainCamera == cb);                 // higher Depth wins among Main cameras
        GameObject* parent = s.CreateGameObject("Rig");
        b->transform->SetParent(parent->transform, true);
        parent->active = false; s.Update(0.016f);
        CHECK(s.mainCamera == ca);                 // inactive parent hides the camera
    }
    {   // New settings: zoom narrows the view, lens shift moves the image, the
        // picking ray matches what is drawn, and shake settles back to rest.
        Scene s; s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Cam"); auto* c = g->AddComponent<Camera>();
        g->transform->localPosition = {0, 0, 10};
        const float fov1 = c->VerticalFovDegrees(1.0f);
        c->zoom = 2.0f;
        CHECK(c->VerticalFovDegrees(1.0f) < fov1 * 0.6f);
        c->zoom = 1.0f; c->lensShiftX = 0.25f;
        Vec2 px;
        CHECK(c->WorldToScreen({0, 0, 0}, 800, 600, px));
        CHECK(px.x < 400.0f - 1.0f);                // shifted right: the centre moves left
        Vec3 o, d;                                  // the ray through that pixel hits the origin
        c->ScreenPointToRay(px.x, px.y, 800, 600, o, d);
        const float t = -o.z / d.z;
        CHECK_NEAR(o.x + d.x * t, 0.0f, 1e-3f);
        CHECK_NEAR(o.y + d.y * t, 0.0f, 1e-3f);
        c->lensShiftX = 0.0f;
        s.Start();
        c->Shake(1.0f);
        s.Update(0.05f);
        const Vec3 shaken = c->RenderPosition();
        CHECK(std::fabs(shaken.x) + std::fabs(shaken.y) > 1e-4f);
        for (int i = 0; i < 120; ++i) s.Update(0.016f);
        CHECK(c->ShakeTrauma() == 0.0f);
        CHECK_NEAR(c->RenderPosition().x, 0.0f, 1e-5f);
        c->ZoomTo(3.0f, 0.5f);
        for (int i = 0; i < 40; ++i) s.Update(0.016f);
        CHECK_NEAR(c->zoom, 3.0f, 1e-4f);
        // Settings survive a save / load.
        c->dutch = 12.0f; c->pixelPerfect = true; c->pixelsPerUnit = 16.0f; c->shakeAmount = 0.7f;
        std::string text = SceneSerializer::Serialize(s);
        Scene r; CHECK(SceneSerializer::Deserialize(r, text));
        Camera* rc = nullptr;
        for (const auto& up : r.Objects()) if (auto* cc = up->GetComponent<Camera>()) rc = cc;
        CHECK(rc && std::fabs(rc->zoom - 3.0f) < 1e-4f && std::fabs(rc->dutch - 12.0f) < 1e-4f);
        CHECK(rc && rc->pixelPerfect && std::fabs(rc->pixelsPerUnit - 16.0f) < 1e-4f && std::fabs(rc->shakeAmount - 0.7f) < 1e-4f);
    }
    TEST_MAIN_RESULT();
}
