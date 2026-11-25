// module:survival — the survival record round-trips while the module is on. With it off a live
// SurvivalStats writes no line, and a line loaded from a file is kept as text and written back.
#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("module:survival");
    ModuleRegistry::Get();
    Scene scene("camp");
    SurvivalStats* sv = scene.CreateGameObject("Player")->AddComponent<SurvivalStats>();
    sv->maxHunger = 80.0f;
    sv->godMode = true;
    const std::string first = SceneSerializer::Serialize(scene);
    CHECK(first.find("\n  survival ") != std::string::npos);
    Scene loaded("loaded");
    CHECK(SceneSerializer::Deserialize(loaded, first));
    CHECK(SceneSerializer::Serialize(loaded) == first);
    SurvivalStats* back = loaded.Find("Player")->GetComponent<SurvivalStats>();
    CHECK(back && back->maxHunger == 80.0f && back->godMode);
    auto at = first.find("\n  survival ");
    auto end = first.find('\n', at + 1);
    std::cout << "survival on: " << first.substr(at + 1, end - at - 1) << "\n";

    ModuleManifest saved = ModuleRegistry::Get().Find("survival")->manifest;
    ModuleRegistry::Get().Add(saved, false);
    CHECK(SceneSerializer::Serialize(scene).find("\n  survival ") == std::string::npos);
    Scene skipped("skipped");
    CHECK(SceneSerializer::Deserialize(skipped, first));
    CHECK(skipped.Find("Player")->GetComponent<SurvivalStats>() == nullptr);
    CHECK(SceneSerializer::Serialize(skipped).find(first.substr(at, end - at + 1)) != std::string::npos);   // not dropped by load then save
    std::cout << "survival off: " << (SceneSerializer::Serialize(scene).find("\n  survival ") == std::string::npos
              ? "(no survival line)" : "still present") << "\n";
    ModuleRegistry::Get().Add(saved, true);
    TEST_MAIN_RESULT();
}
