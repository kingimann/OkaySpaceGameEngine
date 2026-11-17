// The Player Input component (LocalPlayerInput): joins a local player when the scene starts, with its device, scheme, action maps and
// asset; leaves when removed. Real Input fed by hand; no window.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Prefs.hpp"
#include "okay/Input/InputAsset.hpp"
#include <filesystem>

using namespace okay;

int main() {
    RUN_SUITE("local_player_input");
    Prefs::Clear(); InputRuntime::Reset();
    const std::string file = (std::filesystem::temp_directory_path() / "okay_lpi_test.okayinput").string();
    {   // an asset with one extra action
        ActionSet a = DefaultInputAsset();
        a.Add("Zoom", ValueType::Button, "Gameplay").AddBinding("key:z", "Keyboard+Mouse");
        CHECK(SaveInputAsset(file, a));
    }
    Scene scene("lpi"); scene.physicsEnabled = false;
    auto* kb = scene.CreateGameObject("Player 1")->AddComponent<LocalPlayerInput>();
    kb->device = LocalPlayerInput::Device::Keyboard; kb->actions = file; kb->startMaps = "Gameplay";
    auto* pad = scene.CreateGameObject("Player 2")->AddComponent<LocalPlayerInput>();
    pad->device = LocalPlayerInput::Device::Controller; pad->controller = 1; pad->startMaps = " Gameplay ";   // spaces are trimmed
    auto* extra = scene.CreateGameObject("Player 3")->AddComponent<LocalPlayerInput>();
    extra->device = LocalPlayerInput::Device::Keyboard;   // the keyboard is taken
    CHECK(kb->GetPlayerIndex() == -1);                    // not playing yet
    scene.Start();
    CHECK(kb->GetPlayerIndex() == 0 && pad->GetPlayerIndex() == 1 && extra->GetPlayerIndex() == -1);
    CHECK(InputRuntime::AssetPath() == file && kb->Set()->Find("Zoom") && pad->Set()->Find("Zoom"));
    CHECK(kb->Set()->Scheme() == "Keyboard+Mouse" && pad->Set()->Scheme() == "Gamepad");
    CHECK(kb->Set()->MapEnabled("Gameplay") && !kb->Set()->MapEnabled("UI") && pad->Set()->MapEnabled("Gameplay") && !pad->Set()->MapEnabled("UI"));
    CHECK(InputRuntime::Players().PadOf(1) == 1);
    // each reads its own device
    Input::FeedKeys({'w', 'z'});
    Input::FeedGamepadRaw(1, {0.0f, -1.0f}, {}, 0, 0, 1u << Input::PadA, true);
    InputActions::Tick(0.016f);
    CHECK(kb->Set()->Vector("Move").y > 0.9f && kb->Set()->Held("Zoom") && !kb->Set()->Held("Jump"));
    CHECK(pad->Set()->Vector("Move").y < -0.9f && pad->Set()->Held("Jump") && !pad->Set()->Held("Zoom"));
    // a scheme and auto switch can be chosen
    Scene other("lpi2"); other.physicsEnabled = false;
    InputRuntime::Reset();
    auto* any = other.CreateGameObject("Solo")->AddComponent<LocalPlayerInput>();
    any->autoSwitch = false;
    other.Start();
    CHECK(any->GetPlayerIndex() == 0 && !any->Set()->AutoSwitch());
    // removing the component frees the player (and the device)
    other.Destroy(other.Find("Solo"));
    other.Update(0.016f);
    CHECK(!InputRuntime::Players().Active(0));
    std::remove(file.c_str());
    InputRuntime::Reset(); Prefs::Clear(); Input::FeedKeys({});
    TEST_MAIN_RESULT();
}
