#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

int main() {
    RUN_SUITE("camera_cine");

    const float W = 800.0f, H = 600.0f;

    // ScreenPointToRay round-trips WorldToScreen: a point projected to a pixel, then
    // turned back into a ray, yields a ray pointing at that world point.
    {
        Scene s("R"); s.physicsEnabled = false;
        auto* cam = s.CreateGameObject("Cam")->AddComponent<Camera>();   // at origin, looks -Z
        Vec3 P{1.0f, 0.5f, -5.0f};
        Vec2 px;
        CHECK(cam->WorldToScreen(P, W, H, px));
        Vec3 o, d;
        cam->ScreenPointToRay(px.x, px.y, W, H, o, d);
        Vec3 toP = (P - o).Normalized();
        CHECK(Vec3::Dot(d, toP) > 0.999f);                 // ray aims at the point

        // ScreenToWorldPoint at the centre, 5 units ahead, lands straight in front.
        Vec3 c = cam->ScreenToWorldPoint(W * 0.5f, H * 0.5f, 5.0f, W, H);
        CHECK(std::fabs(c.x) < 1e-3f && std::fabs(c.y) < 1e-3f);
        CHECK(std::fabs(c.z + 5.0f) < 1e-2f);

        // A point dead ahead maps to the viewport centre and is visible; a point
        // behind the camera is not.
        Vec2 vp; float vd;
        CHECK(cam->WorldToViewportPoint({0, 0, -5}, W, H, vp, &vd));
        CHECK(std::fabs(vp.x - 0.5f) < 1e-3f && std::fabs(vp.y - 0.5f) < 1e-3f);
        CHECK(cam->IsPointVisible({0, 0, -5}, W, H));
        CHECK(!cam->IsPointVisible({0, 0, 5}, W, H));      // behind the camera

        // ViewportPointToRay at the centre aims straight forward (-Z).
        Vec3 ro, rd;
        cam->ViewportPointToRay(0.5f, 0.5f, W, H, ro, rd);
        CHECK(Vec3::Dot(rd, Vec3{0, 0, -1}) > 0.999f);
    }

    // Cinemachine Confiner: the solved camera position is clamped inside the box even
    // when the follow target is far outside it.
    {
        Scene s("C"); s.physicsEnabled = false;
        auto* tgt = s.CreateGameObject("T");
        tgt->transform->localPosition = {100.0f, 100.0f, 100.0f};
        auto* vc = s.CreateGameObject("VC")->AddComponent<VirtualCamera>();
        vc->follow = "T"; vc->followOffset = {0, 0, 0};
        vc->positionDamping = 0.0f;
        vc->confine = true; vc->confineMin = {-5, -5, -5}; vc->confineMax = {5, 5, 5};
        for (int i = 0; i < 4; ++i) vc->Solve(0.1f);
        Vec3 p = vc->SolvedPosition();
        CHECK(p.x <= 5.001f && p.y <= 5.001f && p.z <= 5.001f);
        CHECK(p.x >= -5.001f && p.y >= -5.001f && p.z >= -5.001f);
    }

    // Dutch + confiner settings round-trip through the scene file.
    {
        Scene a("A"); a.physicsEnabled = false;
        auto* vc = a.CreateGameObject("VC")->AddComponent<VirtualCamera>();
        vc->dutch = 15.0f; vc->confine = true;
        vc->confineMin = {-3, -2, -1}; vc->confineMax = {3, 2, 1};
        std::string text = SceneSerializer::Serialize(a);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lv = b.Find("VC") ? b.Find("VC")->GetComponent<VirtualCamera>() : nullptr;
        CHECK(lv != nullptr);
        if (lv) {
            CHECK(std::fabs(lv->dutch - 15.0f) < 1e-3f);
            CHECK(lv->confine);
            CHECK(std::fabs(lv->confineMax.x - 3.0f) < 1e-3f);
            CHECK(std::fabs(lv->confineMin.y + 2.0f) < 1e-3f);
        }
    }

    TEST_MAIN_RESULT();
}
