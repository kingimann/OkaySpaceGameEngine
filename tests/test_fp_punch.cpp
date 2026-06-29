#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// The punch must swing the arm FORWARD (the body faces -Z, which is also where the
// first-person camera looks), so the hand lands in front of the shoulder — not behind
// it (the old bug: the arm swung backward, "facing the opposite way" in first person).
int main() {
    RUN_SUITE("fp_punch");

    Scene s("A");
    auto* player = s.CreateGameObject("Player");
    auto* ch = player->AddComponent<Character>();
    ch->Apply(); ch->separateParts = true;
    s.Start();
    for (int i = 0; i < 2; ++i) s.Update(0.016f);

    GameObject* shoulder = ch->Part(6);   // R upper arm
    GameObject* hand     = ch->Part(8);   // R hand
    CHECK(shoulder != nullptr);
    CHECK(hand != nullptr);

    ch->Punch();
    CHECK(ch->Punching());

    // Track how far forward (-Z) the hand reaches over the swing.
    float minZ = 1e9f, shoulderZ = shoulder ? shoulder->transform->Position().z : 0.0f;
    for (int i = 0; i < 20 && ch->Punching(); ++i) {
        s.Update(0.02f);
        float z = hand->transform->Position().z;
        if (z < minZ) minZ = z;
    }
    // Forward is -Z: at the peak the hand should be clearly in front of the shoulder.
    CHECK(minZ < shoulderZ - 0.2f);

    TEST_MAIN_RESULT();
}
