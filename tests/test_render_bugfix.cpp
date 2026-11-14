// Regression tests for renderer bugs found in an audit (CPU side, headless).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/Lightmap.hpp"
#include <cmath>

using namespace okay;

static Mat4 CamVP(const Vec3& eye) {
    const Mat4 view = Mat4::LookAt(eye, {0, 0, 0}, {0, 1, 0});
    const Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.1f, 500.0f);
    return proj * view;
}

int main() {
    RUN_SUITE("transparent meshes draw far to near");
    {
        Scene s;
        // Scene order is near, far, middle: the old list kept exactly this order.
        const float zs[3] = {2.0f, -6.0f, -2.0f};
        GameObject* made[3];
        for (int i = 0; i < 3; ++i) {
            GameObject* go = s.CreateGameObject("Glass");
            go->transform->localPosition = {0, 0, zs[i]};
            auto* mr = go->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Cube();
            mr->color = Color(1, 1, 1, 0.5f);
            made[i] = go;
        }
        GameObject* solid = s.CreateGameObject("Solid");
        solid->transform->localPosition = {3, 0, -3};
        auto* smr = solid->AddComponent<MeshRenderer>();
        smr->mesh = Mesh::Cube();
        const Vec3 eye{0, 0, 10};
        const Mat4 view = Mat4::LookAt(eye, {0, 0, 0}, {0, 1, 0});
        const Mat4 vp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 500.0f) * view;
        const visibility::Frame& f = visibility::Build(s, vp, eye, 256, 256, 60, nullptr);
        CHECK(f.items.size() == 4u);
        if (f.items.size() == 4u) {
            CHECK(f.items[0].go == solid);          // opaque first
            CHECK(f.items[1].go == made[1]);        // z = -6, farthest
            CHECK(f.items[2].go == made[2]);        // z = -2
            CHECK(f.items[3].go == made[0]);        // z = +2, nearest, drawn last
        }
    }

    TEST_MAIN_RESULT();
}
