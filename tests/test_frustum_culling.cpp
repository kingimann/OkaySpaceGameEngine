#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/Culling.hpp"
#include <cmath>

using namespace okay;

int main() {
    RUN_SUITE("frustum_culling");

    // A camera at z=5 looking at the origin, 60 degree FOV.
    Mat4 vp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 50.0f) * Mat4::LookAt(Vec3{0, 0, 5}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    Frustum f = Frustum::FromVP(vp);
    CHECK(f.SphereVisible({0, 0, 0}, 0.5f));          // in front
    CHECK(!f.SphereVisible({0, 0, 10}, 0.5f));        // behind the camera
    CHECK(!f.SphereVisible({20, 0, 0}, 0.5f));        // far off to the side
    CHECK(!f.SphereVisible({0, 0, -60}, 0.5f));       // beyond the far plane
    CHECK(f.SphereVisible({3.2f, 0, 0}, 0.5f));       // straddling the right edge (half-width at z=0 is ~2.9)
    CHECK(f.SphereVisible({20, 0, 0}, 18.0f));        // big enough to reach in

    Scene s("cull");
    GameObject* a = s.CreateGameObject("Seen");
    auto* ma = a->AddComponent<MeshRenderer>(); ma->mesh = Mesh::Cube();
    GameObject* b = s.CreateGameObject("Off");
    auto* mb = b->AddComponent<MeshRenderer>(); mb->mesh = Mesh::Cube();
    b->transform->localPosition = {30, 0, 0};
    CHECK(!CulledByFrustum(f, a, ma));
    CHECK(CulledByFrustum(f, b, mb));
    // A huge scale makes the far cube reach the view.
    b->transform->localScale = {50, 50, 50};
    CHECK(!CulledByFrustum(f, b, mb));
    // Bounds follow edits to the mesh (moved vertices, same array).
    b->transform->localScale = {1, 1, 1};
    for (auto& v : mb->mesh.vertices) v.x -= 30.0f;   // the geometry now sits at the origin
    CHECK(!CulledByFrustum(f, b, mb));
    // Off switch.
    for (auto& v : mb->mesh.vertices) v.x += 60.0f;
    CHECK(CulledByFrustum(f, b, mb));
    FrustumCullingEnabled() = false;
    CHECK(!CulledByFrustum(f, b, mb));
    FrustumCullingEnabled() = true;

    // Skinned: the sphere follows the bones, not the bind pose.
    GameObject* body = s.CreateGameObject("Body");
    GameObject* bone = s.CreateGameObject("Bone");
    auto* mr = body->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube();
    auto* sm = body->AddComponent<SkinnedMesh>();
    sm->bind = mr->mesh; sm->joints = {bone->transform};
    sm->jointIdx.assign(mr->mesh.vertices.size(), {0, 0, 0, 0});
    sm->jointWt.assign(mr->mesh.vertices.size(), {1, 0, 0, 0});
    bone->transform->localPosition = {0, 0, 0};
    CHECK(!CulledByFrustum(f, body, mr));
    bone->transform->localPosition = {40, 0, 0};       // the animation carried it away
    CHECK(CulledByFrustum(f, body, mr));

    TEST_MAIN_RESULT();
}
