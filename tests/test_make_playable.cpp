// Make Playable Character: a third-person player, the model, and Look-At IK.
#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneTemplates.hpp"

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

    // A scene that already has a player: the new character is the one played.
    // The previous avatar is deactivated, not destroyed.
    {
        Scene occupied;
        Templates::ThirdPerson(occupied);
        GameObject* old = occupied.Find("Player");
        CHECK(old && old->active);
        CHECK(IsPlayableAvatar(old));
        std::string parked;
        GameObject* neu = MakePlayableCharacter(occupied, righarness::RigPath("mixamo_t_noclips.glb"), &parked);
        CHECK(neu && neu != old);
        CHECK(neu->active);
        CHECK(IsPlayableAvatar(neu));
        CHECK(!old->active);
        CHECK(!old->IsDestroyed());
        CHECK(parked.find("Deactivated") != std::string::npos);
        CHECK(parked.find("Player") != std::string::npos);
        int activeAvatars = 0;
        for (const auto& up : occupied.Objects())
            if (up->active && IsPlayableAvatar(up.get())) ++activeAvatars;
        CHECK(activeAvatars == 1);
    }

    // One selected player wears the model. No second avatar is created.
    {
        Scene worn;
        GameObject* host = Templates::AddThirdPersonPlayer(worn, {0.0f, 1.0f, 0.0f});
        CHECK(IsPlayableAvatar(host));
        std::string wornLog;
        GameObject* same = MakePlayableCharacter(worn, righarness::RigPath("mixamo_t_noclips.glb"), &wornLog, host);
        CHECK(same == host);
        CHECK(host->active);
        CHECK(host->GetComponent<LookAtIK>() != nullptr);
        int avatars = 0;
        for (const auto& up : worn.Objects())
            if (IsPlayableAvatar(up.get())) ++avatars;
        CHECK(avatars == 1);
        CHECK(wornLog.find("Deactivated") == std::string::npos);
    }
    TEST_MAIN_RESULT();
}
