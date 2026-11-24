// module:weapons — the crosshair scene record is written by the weapons module
// at its old place, so save then load then save matches. With weapons off the line is left out.
#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/Crosshair.hpp"
#include <iostream>
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("module:weapons");
    ModuleRegistry::Get();
    Scene scene("hud");
    GameObject* go = scene.CreateGameObject("Hud");
    Crosshair* cr = go->AddComponent<Crosshair>();
    cr->gap = 8.0f;
    cr->spread = 3.0f;
    cr->circle = true;
    cr->spreadRecover = 12.0f;
    const std::string first = SceneSerializer::Serialize(scene);
    CHECK(first.find("\n  crosshair ") != std::string::npos);
    Scene loaded("loaded");
    CHECK(SceneSerializer::Deserialize(loaded, first));
    const std::string second = SceneSerializer::Serialize(loaded);
    CHECK(first == second);
    auto lineOf = [](const std::string& text, const char* key) {
        auto at = text.find(key);
        if (at == std::string::npos) return std::string("(no ") + key + " line)";
        auto end = text.find('\n', at);
        return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    };
    std::cout << "weapons on: " << lineOf(first, "crosshair") << "\n";

    ModuleManifest saved = ModuleRegistry::Get().Find("weapons")->manifest;
    ModuleRegistry::Get().Add(saved, false);
    const std::string hidden = SceneSerializer::Serialize(scene);
    CHECK(hidden.find("\n  crosshair ") == std::string::npos);
    Scene skipped("skipped");
    CHECK(SceneSerializer::Deserialize(skipped, first));
    GameObject* hud = skipped.Find("Hud");
    CHECK(hud && hud->GetComponent<Crosshair>() == nullptr);
    std::cout << "weapons off: " << lineOf(hidden, "crosshair") << "\n";
    ModuleRegistry::Get().Add(saved, true);
    TEST_MAIN_RESULT();
}
