#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Render/Lighting.hpp"
#include <cmath>

using namespace okay;

// New / light-less scenes must not render dark, the cameras the editor puts in
// must actually see the scene (2D included), and deleting a child must not
// leave a component-less ghost under its parent.
int main() {
    RUN_SUITE("scene_defaults");

    {   // No Light object: a default sun still shades the GPU paths.
        Scene s;
        ApplySceneLight(s);
        CHECK(SceneLights::List().size() == 1);
        CHECK(SceneLights::List()[0].type == (int)Light::Type::Directional);
        CHECK(SceneLights::List()[0].color.x > 0.5f);
        // A real light replaces it.
        GameObject* l = s.CreateGameObject("Sun");
        l->AddComponent<Light>()->intensity = 2.0f;
        ApplySceneLight(s);
        CHECK(SceneLights::List().size() == 1);
        CHECK(SceneLights::List()[0].color.x > 1.5f);
    }

    {   // A camera that isn't flagged main is promoted (the Game view uses it).
        Scene s;
        GameObject* c = s.CreateGameObject("Cam");
        auto* cam = c->AddComponent<Camera>();
        cam->main = false;
        bool made = true;
        CHECK(EnsureSceneCamera(s, false, &made) == cam);
        CHECK(!made && cam->main && s.mainCamera == cam);
        // An inactive camera loses to an active one.
        GameObject* c2 = s.CreateGameObject("Cam2");
        auto* cam2 = c2->AddComponent<Camera>();
        c->active = false; s.mainCamera = nullptr;
        CHECK(EnsureSceneCamera(s, false) == cam2);
    }

    {   // Default background is not black; a 2D camera at z = 0 sees sprites at z = 0.
        Camera cam;
        CHECK(cam.backgroundColor.r + cam.backgroundColor.g + cam.backgroundColor.b > 0.3f);
        Scene s;
        GameObject* go = s.CreateGameObject("Cam2D");
        auto* oc = go->AddComponent<Camera>();
        oc->projection = Camera::Projection::Orthographic;
        Vec2 px; float depth = 0.0f;
        CHECK(oc->WorldToScreen(Vec3{1.0f, 1.0f, 0.0f}, 800, 600, px, &depth));
        CHECK(px.x > 400.0f && px.y < 300.0f);
    }

    {   // Deleting a child leaves no Transform-only ghost under the parent.
        Scene s;
        GameObject* p = s.CreateGameObject("Parent");
        GameObject* ch = s.CreateGameObject("Main Camera");
        ch->AddComponent<Camera>();
        ch->transform->SetParent(p->transform, false);
        s.Destroy(ch);
        s.FlushDestroyed();
        CHECK(p->transform->Children().empty());
        CHECK(s.Objects().size() == 1);
    }

    TEST_MAIN_RESULT();
}
