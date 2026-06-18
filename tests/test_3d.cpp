#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

static bool NearIdentity(const Mat4& m, float eps = 1e-3f) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float expect = (c == r) ? 1.0f : 0.0f;
            if (Mathf::Abs(m.at(c, r) - expect) > eps) return false;
        }
    return true;
}

int main() {
    RUN_SUITE("3d");

    // Mat4 inverse: M * M^-1 == I for a full TRS matrix.
    Mat4 trs = Mat4::TRS({3, -2, 5}, Quat::Euler(20, 40, 60), {2, 0.5f, 1.5f});
    CHECK(NearIdentity(trs * trs.Inverse()));

    // LookRotation: looking along +Z is identity; looking along +X maps Z->X.
    Quat idLook = Quat::LookRotation(Vec3::Forward, Vec3::Up);
    CHECK(NearIdentity(Mat4::Rotate(idLook)));
    Quat lookX = Quat::LookRotation(Vec3::Right, Vec3::Up);
    Vec3 mapped = lookX * Vec3::Forward; // local forward -> world +X
    CHECK_NEAR(mapped.x, 1.0f, 1e-3);
    CHECK_NEAR(mapped.y, 0.0f, 1e-3);
    CHECK_NEAR(mapped.z, 0.0f, 1e-3);

    // LookAt view matrix sends the eye position to the origin.
    Mat4 view = Mat4::LookAt({0, 0, 10}, Vec3::Zero, Vec3::Up);
    Vec3 eyeInView = view.MultiplyPoint({0, 0, 10});
    CHECK_NEAR(eyeInView.x, 0.0f, 1e-3);
    CHECK_NEAR(eyeInView.y, 0.0f, 1e-3);
    CHECK_NEAR(eyeInView.z, 0.0f, 1e-3);

    // Mesh primitives.
    Mesh cube = Mesh::Cube(2.0f);
    CHECK(cube.vertices.size() == 8);
    CHECK(cube.TriangleCount() == 12);
    CHECK(Mesh::Quad().TriangleCount() == 2);
    CHECK(Mesh::Pyramid().vertices.size() == 5);

    // Camera matrices via a perspective camera looking down -Z.
    Scene scene("3d");
    GameObject* camObj = scene.CreateGameObject("Cam");
    camObj->transform->localPosition = {0, 0, 10};
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    scene.Start();

    Mat4 v = cam->ViewMatrix();
    Vec3 originInView = v.MultiplyPoint(Vec3::Zero); // world origin is 10 in front
    CHECK_NEAR(originInView.z, -10.0f, 1e-2); // camera looks down -Z

    Mat4 proj = cam->ProjectionMatrix(16.0f / 9.0f);
    Vec3 clip = proj.MultiplyPoint(originInView); // should be within the cube
    CHECK(Mathf::Abs(clip.x) <= 1.5f);
    CHECK(Mathf::Abs(clip.y) <= 1.5f);

    TEST_MAIN_RESULT();
}
