// module:vehicles — vehicle, vehicletune, vehiclesusp and vehicle2d round-trip while the
// module is on, and are left out of the file when it is off.
#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleController2D.hpp"
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("module:vehicles");
    ModuleRegistry::Get();
    Scene scene("road");
    VehicleController* v = scene.CreateGameObject("Car")->AddComponent<VehicleController>();
    v->acceleration = 18.0f;
    v->wheelBase = 3.1f;
    v->suspension = true;
    v->rideHeight = 0.35f;
    VehicleController2D* v2 = scene.CreateGameObject("Kart")->AddComponent<VehicleController2D>();
    v2->maxSpeed = 22.0f;
    v2->sideView = true;
    const std::string first = SceneSerializer::Serialize(scene);
    CHECK(first.find("\n  vehicle ") != std::string::npos);
    CHECK(first.find("\n  vehicletune ") != std::string::npos);
    CHECK(first.find("\n  vehiclesusp ") != std::string::npos);
    CHECK(first.find("\n  vehicle2d ") != std::string::npos);
    Scene loaded("loaded");
    CHECK(SceneSerializer::Deserialize(loaded, first));
    CHECK(SceneSerializer::Serialize(loaded) == first);
    VehicleController* back = loaded.Find("Car")->GetComponent<VehicleController>();
    CHECK(back && back->acceleration == 18.0f && back->suspension && back->wheelBase == 3.1f);
    CHECK(loaded.Find("Kart")->GetComponent<VehicleController2D>()->sideView);
    auto lineOf = [](const std::string& text, const char* key) {
        auto at = text.find(std::string("\n  ") + key);
        if (at == std::string::npos) return std::string("(no ") + key + " line)";
        auto end = text.find('\n', at + 1);
        return text.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
    };
    std::cout << "vehicles on: " << lineOf(first, "vehicle ") << "\n";
    std::cout << "vehicles on: " << lineOf(first, "vehicletune") << "\n";
    std::cout << "vehicles on: " << lineOf(first, "vehiclesusp") << "\n";
    std::cout << "vehicles on: " << lineOf(first, "vehicle2d") << "\n";

    ModuleManifest saved = ModuleRegistry::Get().Find("vehicles")->manifest;
    ModuleRegistry::Get().Add(saved, false);
    const std::string hidden = SceneSerializer::Serialize(scene);
    CHECK(hidden.find("\n  vehicle ") == std::string::npos);
    CHECK(hidden.find("\n  vehicle2d ") == std::string::npos);
    Scene skipped("skipped");
    CHECK(SceneSerializer::Deserialize(skipped, first));
    CHECK(skipped.Find("Car")->GetComponent<VehicleController>() == nullptr);
    CHECK(skipped.Find("Kart")->GetComponent<VehicleController2D>() == nullptr);
    std::cout << "vehicles off: " << lineOf(hidden, "vehicle ") << "\n";
    std::cout << "vehicles off: " << lineOf(hidden, "vehicle2d") << "\n";
    ModuleRegistry::Get().Add(saved, true);
    TEST_MAIN_RESULT();
}
