#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("clicktomove");

    // MoveTo sets a destination; the player walks there and stops on arrival.
    {
        Scene s("CTM"); s.physicsEnabled = true;
        GameObject* p = s.CreateGameObject("Player");
        auto* rb = p->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;                    // isolate XZ steering
        auto* cm = p->AddComponent<ClickToMoveController>();
        cm->walkSpeed = 5.0f;
        s.Start();

        cm->MoveTo({6, 0, 0});
        CHECK(cm->HasDestination());
        for (int i = 0; i < 240 && cm->HasDestination(); ++i) s.Update(1.0f / 60.0f);
        CHECK(!cm->HasDestination());               // arrived
        CHECK(Mathf::Abs(p->transform->Position().x - 6.0f) < 0.2f);
    }

    // Serialization round-trips the controller's settings.
    {
        Scene s("Persist"); s.physicsEnabled = false;
        GameObject* p = s.CreateGameObject("P");
        auto* cm = p->AddComponent<ClickToMoveController>();
        cm->walkSpeed = 3.5f; cm->runSpeed = 9.0f; cm->mouseButton = 1; cm->holdToMove = true;
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* c2 = s2.Find("P") ? s2.Find("P")->GetComponent<ClickToMoveController>() : nullptr;
        CHECK(c2 != nullptr);
        if (c2) {
            CHECK_NEAR(c2->walkSpeed, 3.5f, 1e-4f);
            CHECK_NEAR(c2->runSpeed, 9.0f, 1e-4f);
            CHECK(c2->mouseButton == 1);
            CHECK(c2->holdToMove == true);
        }
    }

    TEST_MAIN_RESULT();
}
