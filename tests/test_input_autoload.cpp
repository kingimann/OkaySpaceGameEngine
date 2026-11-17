// A play session in the editor uses the project's Assets/input.okayinput when it exists (the default asset otherwise), so the
// input actions work with no setup; a Player Input component with its own asset path replaces it. EditorState is ImGui-free.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "okay/Components/LocalPlayerInput.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/InputAsset.hpp"

#include <filesystem>

using namespace okay;
using okay::editor::EditorState;
namespace fs = std::filesystem;

int main() {
    RUN_SUITE("input_autoload");
    Prefs::Clear(); InputRuntime::Reset();
    const fs::path root = fs::temp_directory_path() / "okay_input_autoload_project";
    fs::remove_all(root); fs::create_directories(root / "Assets");
    const std::string projectFile = (root / "Assets" / "input.okayinput").string();
    {
        ActionSet a = DefaultInputAsset();
        a.Add("Zoom", ValueType::Button, "Gameplay").AddBinding("key:z", "Keyboard+Mouse");
        CHECK(SaveInputAsset(projectFile, a));
    }
    EditorState ed;
    ed.setProjectDir(root.string());
    // Play: the project's asset, and the single default player works with no component at all
    ed.Play();
    CHECK(ed.isPlaying());
    CHECK(InputRuntime::AssetPath() == projectFile);
    ActionSet* s = InputRuntime::Set(0);
    CHECK(s && s->Find("Zoom") && s->Find("Move"));
    Input::FeedKeys({'z'}); InputActions::Tick(0.016f);
    CHECK(s->Held("Zoom"));
    ed.Stop();
    CHECK(!InputRuntime::Players().Active(0));                 // the session's players are gone
    // a project without the file: the default asset
    fs::remove(projectFile);
    Input::FeedKeys({});
    ed.Play();
    CHECK(InputRuntime::AssetPath().empty() && InputRuntime::Set(0) && !InputRuntime::Set(0)->Find("Zoom") && InputRuntime::Set(0)->Find("Jump"));
    ed.Stop();
    // a Player Input with its own asset path wins over the project's
    {
        ActionSet a = DefaultInputAsset();
        a.Add("Dash", ValueType::Button, "Gameplay").AddBinding("key:q", "Keyboard+Mouse");
        const std::string own = (root / "Assets" / "own.okayinput").string();
        CHECK(SaveInputAsset(own, a));
        SaveInputAsset(projectFile, DefaultInputAsset());
        auto* lp = ed.scene().CreateGameObject("P")->AddComponent<LocalPlayerInput>();
        lp->actions = own; lp->device = LocalPlayerInput::Device::Keyboard;
        ed.Play();
        ed.Tick(0.016f);   // the first tick starts the components
        CHECK(lp->GetPlayerIndex() == 0 && lp->Set() && lp->Set()->Find("Dash"));
        CHECK(InputRuntime::AssetPath() == own);
        ed.Stop();
    }
    // no project folder at all: the default asset
    EditorState bare;
    bare.Play();
    CHECK(InputRuntime::AssetPath().empty() && InputRuntime::Set(0) && InputRuntime::Set(0)->Find("Move"));
    bare.Stop();
    fs::remove_all(root);
    InputRuntime::Reset(); Prefs::Clear(); Input::FeedKeys({});
    TEST_MAIN_RESULT();
}
