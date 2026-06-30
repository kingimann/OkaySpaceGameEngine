#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

static bool HasCameraInSubtree(GameObject* go) {
    if (!go) return false;
    if (go->GetComponent<Camera>()) return true;
    if (go->transform)
        for (Transform* c : go->transform->Children())
            if (c && HasCameraInSubtree(c->gameObject)) return true;
    return false;
}

// NetworkPlayerSpawner clones a player template into a fully-driveable LOCAL player and
// into stripped-down remote proxies (no input/camera, with a motion driver).
int main() {
    RUN_SUITE("net_player_spawn");

    Scene s("mp");
    // A player template: Character + controller, with a camera + FP hand child.
    auto* tmpl = s.CreateGameObject("PlayerTemplate");
    tmpl->AddComponent<Character>()->Apply();
    tmpl->AddComponent<FirstPersonController>();
    auto* tcam = s.CreateGameObject("Eye");
    tcam->transform->SetParent(tmpl->transform, false);
    tcam->AddComponent<Camera>();
    tcam->AddComponent<FirstPersonHand>();

    s.CreateGameObject("Net")->AddComponent<NetworkManager>();
    auto* sp = s.CreateGameObject("Spawner")->AddComponent<NetworkPlayerSpawner>();
    sp->playerTemplate = "PlayerTemplate";
    sp->spawnPoint = {5, 0, 5};

    s.Start();

    // Template is hidden so it isn't a live player.
    CHECK(!tmpl->active);

    // Local player spawned, active, and KEEPS its controller + camera.
    GameObject* me = s.Find("LocalPlayer");
    CHECK(me != nullptr);
    if (me) {
        CHECK(me->active);
        CHECK(me->GetComponent<FirstPersonController>() != nullptr);
        CHECK(HasCameraInSubtree(me));                       // viewmodel camera intact
        CHECK(me->GetComponent<Character>() != nullptr);
        CHECK(std::abs(me->transform->localPosition.x - 5.0f) < 1e-3f);
    }

    // A remote proxy: stripped of input + camera, with a motion driver, body intact.
    GameObject* proxy = sp->SpawnPlayer(3, false);
    CHECK(proxy != nullptr);
    if (proxy) {
        CHECK(proxy->GetComponent<FirstPersonController>() == nullptr);   // input stripped
        CHECK(!HasCameraInSubtree(proxy));                                // camera stripped
        CHECK(proxy->GetComponent<NetworkAvatarMotion>() != nullptr);     // motion driver added
        CHECK(proxy->GetComponent<Character>() != nullptr);               // still a real character
        CHECK(proxy->active);
    }

    // Config round-trips through the scene file.
    {
        std::string text = SceneSerializer::Serialize(s);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        GameObject* g = b.Find("Spawner");
        auto* lp = g ? g->GetComponent<NetworkPlayerSpawner>() : nullptr;
        CHECK(lp != nullptr);
        if (lp) {
            CHECK(lp->playerTemplate == "PlayerTemplate");
            CHECK(std::abs(lp->spawnPoint.x - 5.0f) < 1e-3f);
        }
    }

    TEST_MAIN_RESULT();
}
