// Make Playable Character: a third-person player, the model, and Look-At IK.
#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Scene/Scene.hpp"

using namespace okay;

int main() {
    RUN_SUITE("make_playable");
    Scene scene; scene.physicsEnabled = false;
    std::string log;
    GameObject* player = MakePlayableCharacter(scene, righarness::RigPath("mixamo_t_noclips.glb"), &log);
    CHECK(player != nullptr);
    if (!player) TEST_MAIN_RESULT();
    CHECK(player->GetComponent<PlayerController>() != nullptr);
    CHECK(player->GetComponent<ThirdPersonController>() != nullptr);
    CHECK(player->GetComponent<FootIK>() != nullptr);
    LookAtIK* look = player->GetComponent<LookAtIK>();
    CHECK(look != nullptr);
    if (look) {
        CHECK(look->ikActive);
        CHECK(!look->aimAtPoint);
        CHECK(!look->chainNames.empty());
    }
    CHECK(log.find("look-at") != std::string::npos);
    TEST_MAIN_RESULT();
}
