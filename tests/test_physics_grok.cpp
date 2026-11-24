// Joint3D linked to an object, not only to a name.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

int main() {
    // Two objects share a name. The joint is tied to the second one by object, not to the first
    // one the name would find, and not to the world origin.
    RUN_SUITE("joint linked by object");
    {
        Scene s("link");
        auto* first = s.CreateGameObject("Post");
        first->transform->localPosition = {0, 0, 0};
        first->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        auto* second = s.CreateGameObject("Post");
        second->transform->localPosition = {4, 0, 0};
        second->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        auto* weight = s.CreateGameObject("Weight");
        weight->transform->localPosition = {4, 2, 0};
        auto* rb = weight->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->velocity = {-3, 0, 0};
        auto* j = weight->AddComponent<Joint3D>();
        j->mode = (int)Joint3D::Mode::Distance;
        j->autoConfigure = false;
        j->distance = 0.5f;
        j->anchor = {0, 0, 0};
        j->ConnectTo(second);
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        CHECK(weight->transform->Position().x > 3.0f);
    }
    TEST_MAIN_RESULT();
}
