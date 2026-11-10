#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <sstream>

using namespace okay;

static Camera* AddCam(Scene& s, const char* name, float depth, float x, float y, float w, float h) {
    GameObject* go = s.CreateGameObject(name);
    go->transform->localPosition = {0, 1, 5};
    auto* c = go->AddComponent<Camera>();
    c->main = depth == 0.0f;
    c->depth = depth;
    c->rectX = x; c->rectY = y; c->rectW = w; c->rectH = h;
    return c;
}

int main() {
    RUN_SUITE("camera stack plan");
    {
        Scene s;
        Camera* a = AddCam(s, "Main", 0, 0, 0, 1, 1);
        s.Start();
        auto one = PlanCameraStack(s, 800, 600, nullptr);
        CHECK(one.singleFullFrame);
        CHECK(one.passes.size() == 1u);
        CHECK(one.passes[0].camera == a);
        CHECK(one.passes[0].w == 800 && one.passes[0].h == 600);
        CHECK(one.passes[0].clearColor && one.passes[0].sky);
        CHECK(!one.passes[0].overlay);

        Camera* b = AddCam(s, "Pip", 2, 0.7f, 0.7f, 0.25f, 0.25f);
        b->main = false;
        b->clearFlags = Camera::ClearFlags::DepthOnly;
        b->cullingMask = 1 << 3;
        b->renderTarget = "pip";
        if (s.mainCamera == b) s.mainCamera = a;
        s.Update(0.016f);
        auto two = PlanCameraStack(s, 800, 600, nullptr);
        CHECK(!two.singleFullFrame);
        CHECK(two.passes.size() == 2u);
        CHECK(two.passes[0].camera == a);
        CHECK(two.passes[1].camera == b);
        CHECK(two.passes[1].overlay);
        CHECK(!two.passes[1].clearColor);
        CHECK(two.passes[1].clearDepth);
        CHECK(two.passes[1].cullingMask == (1 << 3));
        CHECK(two.passes[1].target == "pip");
        CHECK(two.passes[1].w > 10 && two.passes[1].w < 400);
        CHECK(two.passes[1].x > two.passes[0].x);

        // Lowest depth draws first even if it was added second.
        Camera* under = AddCam(s, "Under", -5, 0, 0, 0.5f, 1);
        under->main = false;
        under->clearFlags = Camera::ClearFlags::SolidColor;
        under->backgroundColor = Color(0.2f, 0.1f, 0.0f, 1);
        s.Update(0.016f);
        auto three = PlanCameraStack(s, 200, 100, nullptr);
        CHECK(three.passes[0].camera == under);
        CHECK(three.passes[0].clearA == 1.0f);
        CHECK(!three.passes[0].sky);
    }

    RUN_SUITE("viewport rect projection");
    {
        Scene s;
        Camera* c = AddCam(s, "Cam", 0, 0, 0, 1, 1);
        c->gameObject->transform->localPosition = {0, 0, 5};
        // +Z is the object's forward; a camera looks down -Z, so +Z points away from the origin.
        c->gameObject->transform->localRotation = Quat::LookRotation({0, 0, 1});
        s.Start();
        Vec2 full;
        CHECK(c->WorldToScreen({0, 0, 0}, 200, 100, full));
        c->rectX = 0.5f; c->rectY = 0; c->rectW = 0.5f; c->rectH = 1;
        Vec2 half;
        CHECK(c->WorldToScreen({0, 0, 0}, 200, 100, half));
        // The centre of the view sits in the middle of the rect, not the window.
        CHECK(half.x > full.x);
        CHECK_NEAR(half.y, full.y, 1.5f);
        Vec3 o, d;
        c->ScreenPointToRay(150, 50, 200, 100, o, d);
        CHECK(d.SqrMagnitude() > 0.5f);
        // A full rect still matches the old full-frame mapping.
        c->rectX = 0; c->rectY = 0; c->rectW = 1; c->rectH = 1;
        Vec2 again;
        CHECK(c->WorldToScreen({0, 0, 0}, 200, 100, again));
        CHECK_NEAR(again.x, full.x, 0.01f);
        CHECK_NEAR(again.y, full.y, 0.01f);
    }

    RUN_SUITE("camera stack round trip");
    {
        Scene s;
        Camera* c = AddCam(s, "Main", 1.5f, 0.1f, 0.2f, 0.5f, 0.4f);
        c->clearFlags = Camera::ClearFlags::DontClear;
        c->renderTarget = "scope";
        c->cullingMask = 6;
        s.Start();
        std::string text = SceneSerializer::Serialize(s);
        Scene loaded;
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        Camera* back = nullptr;
        for (const auto& up : loaded.Objects())
            if (auto* cam = up->GetComponent<Camera>()) back = cam;
        CHECK(back != nullptr);
        CHECK(back->clearFlags == Camera::ClearFlags::DontClear);
        CHECK(back->renderTarget == "scope");
        CHECK_NEAR(back->rectX, 0.1f, 1e-4f);
        CHECK(back->cullingMask == 6);
        CHECK_NEAR(back->depth, 1.5f, 1e-4f);
    }

    RUN_SUITE("composite overlay keeps the base");
    {
        Scene s;
        AddCam(s, "A", 0, 0, 0, 1, 1);
        Camera* b = AddCam(s, "B", 1, 0, 0, 1, 1);
        b->main = false;
        b->clearFlags = Camera::ClearFlags::DepthOnly;
        s.Start();
        auto plan = PlanCameraStack(s, 2, 1, nullptr);
        std::vector<std::uint32_t> base = {0xFF0000FFu, 0xFF00FF00u};   // opaque red, green
        std::vector<std::uint32_t> over = {0x00000000u, 0xFFFF0000u};   // hole, opaque blue-ish
        int which = 0;
        std::vector<std::uint32_t> out;
        CHECK(CompositeCameraStack(plan, [&](const CameraPass&) {
            return which++ == 0 ? base.data() : over.data();
        }, out));
        CHECK(out[0] == 0xFF0000FFu);          // overlay hole kept the base
        CHECK((out[1] >> 24) == 0xFFu);        // overlay pixel replaced
    }

    RUN_SUITE("solid clear paints the software rasterizer's holes");
    {
        // The software path clears to alpha 0. A Solid Color camera has to fill
        // those pixels or the inset stays transparent and the host sky shows through.
        Scene s;
        AddCam(s, "A", 0, 0, 0, 1, 1);
        Camera* b = AddCam(s, "B", 2, 0, 0, 1, 1);
        b->main = false;
        b->clearFlags = Camera::ClearFlags::SolidColor;
        b->backgroundColor = Color(0.0f, 0.0f, 1.0f, 1.0f);
        s.Start();
        auto plan = PlanCameraStack(s, 2, 1, nullptr);
        CHECK(plan.passes.size() == 2);
        CHECK(!plan.passes[1].sky);
        CHECK(plan.passes[1].clearA == 1.0f);
        std::vector<std::uint32_t> hole = {0u, 0u};
        int which = 0;
        std::vector<std::uint32_t> out;
        CHECK(CompositeCameraStack(plan, [&](const CameraPass&) {
            return which++ == 0 ? hole.data() : hole.data();
        }, out));
        CHECK((out[0] >> 24) == 0xFFu);
        CHECK(((out[0] >> 16) & 255) > 200);   // opaque blue, not the empty hole
        CHECK(((out[1] >> 16) & 255) > 200);
    }
    TEST_MAIN_RESULT();
}
