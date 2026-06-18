#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("transform_input");

    // Transform point/direction conversions round-trip.
    Scene scene("T");
    GameObject* go = scene.CreateGameObject("Obj");
    go->transform->localPosition = {5, 0, 0};
    go->transform->localRotation = Quat::Euler(0, 0, 90);
    go->transform->localScale = {2, 2, 2};

    Vec3 world = go->transform->TransformPoint({1, 0, 0});
    Vec3 back  = go->transform->InverseTransformPoint(world);
    CHECK(back == Vec3(1, 0, 0));

    // +X local, rotated 90 deg about Z -> +Y direction.
    Vec3 d = go->transform->TransformDirection(Vec3::Right);
    CHECK_NEAR(d.x, 0.0f, 1e-3);
    CHECK_NEAR(d.y, 1.0f, 1e-3);

    // Input::FeedKeys drives held/down/up edges.
    Input::FeedKeys({'w'});
    CHECK(Input::GetKey('w'));
    CHECK(Input::GetKeyDown('w'));
    CHECK(Input::AxisWASD().y > 0.5f);

    Input::FeedKeys({'w'});
    CHECK(Input::GetKey('w'));
    CHECK(!Input::GetKeyDown('w')); // held, not a new press

    Input::FeedKeys({});
    CHECK(!Input::GetKey('w'));
    CHECK(Input::GetKeyUp('w'));

    TEST_MAIN_RESULT();
}
