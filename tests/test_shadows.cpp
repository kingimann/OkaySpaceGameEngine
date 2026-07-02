#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// Cascaded, camera-focused shadow maps: a raised caster shadows the ground
// directly beneath it; open ground stays lit; the near cascade is denser (sharper
// texels) than the far one; legacy whole-scene mode still works.
int main() {
    RUN_SUITE("shadows");

    auto makeMesh = [](Scene& s, const char* name, Mesh m, Vec3 pos, Vec3 scale) {
        GameObject* go = s.CreateGameObject(name);
        go->transform->localPosition = pos;
        go->transform->localScale = scale;
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->mesh = m;
        return go;
    };

    // A camera view-projection looking down the -Z axis from behind the scene.
    const Vec3 eye{0, 6, 18};
    Mat4 view = Mat4::LookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    Mat4 proj = Mat4::Perspective(60.0f, 16.0f / 9.0f, 0.1f, 200.0f);
    Mat4 vp = proj * view;

    // --- Cascaded shadows: caster occludes the ground under it ----------------
    {
        Scene s("shadow");
        makeMesh(s, "Ground", Mesh::Plane(40.0f), Vec3{0, 0, 0}, Vec3{1, 1, 1});
        makeMesh(s, "Caster", Mesh::Cube(2.0f), Vec3{0, 4, 0}, Vec3{1, 1, 1});

        SceneLight::SetDirection(Vec3{0, -1, 0});   // straight down
        ShadowsEnabled() = true;
        ShadowDistance() = 80.0f;
        ShadowCascades() = 3;
        ShadowMapResolution() = 1024;

        RenderShadowMap(s, vp, eye);
        ShadowMap& sm = Shadows();
        CHECK(sm.enabled);
        CHECK(sm.count == 3);

        // Point on the ground directly beneath the cube -> shadowed (factor < 1).
        float under = ShadowFactor(Vec3{0, 0.0f, 0}, Vec3{0, 1, 0});
        // Point well away from the caster -> lit (factor == 1).
        float open  = ShadowFactor(Vec3{14, 0.0f, 0}, Vec3{0, 1, 0});
        CHECK(under < 0.5f);
        CHECK_NEAR(open, 1.0f, 0.001f);
    }

    // --- The near cascade has denser texels than the far one ------------------
    {
        ShadowMap& sm = Shadows();
        CHECK(sm.count >= 2);
        // texelWorld = world size of one shadow texel; smaller = sharper.
        CHECK(sm.cascade[0].texelWorld < sm.cascade[sm.count - 1].texelWorld);
        // The near cascade's region is smaller than the far one's.
        CHECK(sm.cascade[0].radius < sm.cascade[sm.count - 1].radius);
    }

    // --- Disabling shadows clears the map -------------------------------------
    {
        Scene s("noshadow");
        makeMesh(s, "Ground", Mesh::Plane(10.0f), Vec3{0, 0, 0}, Vec3{1, 1, 1});
        ShadowsEnabled() = false;
        RenderShadowMap(s, vp, eye);
        CHECK(!Shadows().enabled);
        CHECK_NEAR(ShadowFactor(Vec3{0, 0, 0}, Vec3{0, 1, 0}), 1.0f, 0.001f);  // no shadow term
    }

    // --- Legacy whole-scene mode (ShadowDistance <= 0) still casts shadows -----
    {
        Scene s("legacy");
        makeMesh(s, "Ground", Mesh::Plane(20.0f), Vec3{0, 0, 0}, Vec3{1, 1, 1});
        makeMesh(s, "Caster", Mesh::Cube(2.0f), Vec3{0, 4, 0}, Vec3{1, 1, 1});
        SceneLight::SetDirection(Vec3{0, -1, 0});
        ShadowsEnabled() = true;
        ShadowDistance() = 0.0f;          // legacy single map fit to the whole scene
        RenderShadowMap(s, vp, eye);
        CHECK(Shadows().enabled);
        CHECK(Shadows().count == 1);
        float under = ShadowFactor(Vec3{0, 0.0f, 0}, Vec3{0, 1, 0});
        CHECK(under < 0.5f);
    }

    // Restore defaults for any later-running suites in the same process.
    ShadowsEnabled() = false;
    ShadowDistance() = 80.0f;
    ShadowCascades() = 3;
    SceneLight::Reset();

    TEST_MAIN_RESULT();
}
