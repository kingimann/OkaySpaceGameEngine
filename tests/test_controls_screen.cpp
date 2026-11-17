// The Controls screen on the typed input system (UIGallery::AddControlsScreen): fields on "ia:<action>@<player>" show the player's
// binding and rebind it, saved as that player's overrides. Real Input fed by hand; no window.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Prefs.hpp"
#include "okay/Scene/UIGallery.hpp"

using namespace okay;

namespace {
UIKeybindField* Field(Scene& s, const char* name) { GameObject* g = s.Find(name); return g ? g->GetComponent<UIKeybindField>() : nullptr; }
void Step(Scene& s, std::vector<char> keys, unsigned pad = 0) {
    Input::FeedKeys(keys);
    Input::FeedGamepadRaw(0, {}, {}, 0, 0, pad, pad != 0 || Input::GamepadConnected(0));
    InputActions::Tick(0.016f);
    s.Update(0.016f);
}
} // namespace

int main() {
    RUN_SUITE("controls_screen");
    Prefs::Clear();
    InputRuntime::Reset();
    Scene scene("controls"); scene.physicsEnabled = false;
    GameObject* root = UIGallery::AddControlsScreen(scene);
    CHECK(root != nullptr);
    scene.Start();
    Step(scene, {});
    // one keyboard field per input (Move has four), a controller field where the action has a controller button
    UIKeybindField* up = Field(scene, "Key Move up");
    UIKeybindField* jump = Field(scene, "Key Jump");
    UIKeybindField* jumpPad = Field(scene, "Pad Jump");
    CHECK(up && jump && jumpPad && !Field(scene, "Pad Move up") && !Field(scene, "Key Look"));
    CHECK(up->KeyLabel() == "W" && jump->KeyLabel() == "Space" && jumpPad->KeyLabel() == "A");
    // a mouse input is a row too
    CHECK(Field(scene, "Key Fire") && Field(scene, "Key Fire")->KeyLabel() == "Left Mouse");

    // rebind Jump to J: click-equivalent, then press the key (the first frame after Listen is skipped)
    jump->Listen(); Step(scene, {}); Step(scene, {'j'});
    CHECK(!jump->IsListening() && jump->KeyLabel() == "J");
    Step(scene, {});
    ActionSet* set = InputRuntime::Players().Player(0);
    CHECK(set && set->Find("Jump")->bindings[0].parts[0] == "key:j");
    Input::FeedKeys({'j'}); set->SetScheme("Keyboard+Mouse"); set->Update(0.016f);
    CHECK(set->Held("Jump"));
    CHECK(Prefs::Has("inputov.player0"));
    // one key of the Move composite
    up->Listen(); Step(scene, {}); Step(scene, {'i'});
    CHECK(up->KeyLabel() == "I" && set->Find("Move")->bindings[0].parts[0] == "key:i" && set->Find("Move")->bindings[0].parts[1] == "key:s");
    // the controller field takes a controller button
    Input::FeedGamepadRaw(0, {}, {}, 0, 0, 0, true);
    jumpPad->Listen(); Step(scene, {}); Step(scene, {}, 1u << Input::PadX);
    CHECK(jumpPad->KeyLabel() == "X" && set->Find("Jump")->bindings[1].parts[0] == "pad:x");
    // Backspace puts the asset's input back; Escape cancels
    Step(scene, {}); jump->Listen(); Step(scene, {}); Step(scene, {'\b'});
    CHECK(jump->KeyLabel() == "Space");
    up->Listen(); Step(scene, {}); Step(scene, {(char)27});
    CHECK(!up->IsListening() && up->KeyLabel() == "I");
    // the saved overrides come back for a new session
    InputRuntime::Reset();
    InputRuntime::Set(0);
    CHECK(InputRuntime::Players().Player(0)->Find("Move")->bindings[0].parts[0] == "key:i" && InputRuntime::Players().Player(0)->Find("Jump")->bindings[0].parts[0] == "key:space");
    CHECK(Field(scene, "Key Move up")->KeyLabel() == "I");
    // a field for a player that does not exist reads None
    Scene other("other"); other.physicsEnabled = false;
    GameObject* r2 = UIGallery::AddControlsScreen(other, nullptr, 3);
    other.Start();
    CHECK(r2 && Field(other, "Key Jump")->KeyLabel() == "None");

    InputRuntime::Reset(); Prefs::Clear(); Input::FeedKeys({});
    TEST_MAIN_RESULT();
}
