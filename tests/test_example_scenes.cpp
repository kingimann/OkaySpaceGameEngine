// File > Examples > UI Gallery: builds the scene and saves it into the open project, so Play needs no "save first" step.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "ExampleScenes.hpp"
#include <Okay.hpp>
#include <filesystem>
#include <fstream>

using namespace okay;
namespace fs = std::filesystem;

int main() {
    RUN_SUITE("example_scenes");
    const fs::path root = fs::path(OKAY_TEST_OUTPUT_DIR) / "example_project";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // No project open: the scene is built, clean, and not saved anywhere.
    {
        okay::editor::EditorState ed;
        std::string where, err;
        CHECK(oked::BuildUIGalleryExample(ed, false, &where, &err));
        CHECK(where.empty() && !ed.dirty);
        CHECK(ed.scene().Find("UI Gallery") != nullptr && ed.scene().Find("ThemePick") != nullptr);
    }

    okay::editor::EditorState ed;
    ed.setProjectDir(root.string());
    std::string where, err;
    CHECK(oked::BuildUIGalleryExample(ed, false, &where, &err));
    CHECK(!where.empty() && fs::exists(where));                       // a real file in the project
    CHECK(where.find("Assets/Scenes/UI Gallery.okayscene") != std::string::npos);
    CHECK(!ed.dirty && ed.path() == where);                           // saved: Play asks nothing
    CHECK(fs::file_size(where) > 10000);

    // It is a scene that opens again, widgets and all.
    {
        okay::editor::EditorState other;
        std::string lerr;
        CHECK(other.Load(where, &lerr));
        CHECK(other.scene().Find("Buttons") && other.scene().Find("Window") && other.scene().Find("Gallery Bag"));
    }

    // Asking again never overwrites: a second file, a third.
    std::string second, third;
    CHECK(oked::BuildUIGalleryExample(ed, false, &second, &err));
    CHECK(oked::BuildUIGalleryExample(ed, true, &third, &err));
    CHECK(second != where && second.find("UI Gallery 2.okayscene") != std::string::npos);
    CHECK(third.find("UI Gallery (phone).okayscene") != std::string::npos);
    CHECK(fs::exists(where) && fs::exists(second) && fs::exists(third));
    CHECK(ed.scene().Find("SwatchScroll") != nullptr);               // the phone layout
    // The editor lays the UI out every frame (scroll content sizes). That must not make the freshly saved example "changed",
    // or Play would ask to save it.
    okay::UpdateUILayouts(ed.scene());
    CHECK(!ed.HasUnsavedChanges());

    // An unwritable place is reported, and the built scene stays open.
    {
        okay::editor::EditorState bad;
        const fs::path blocker = fs::path(OKAY_TEST_OUTPUT_DIR) / "not_a_folder";
        { std::ofstream f(blocker.string()); f << "x"; }
        bad.setProjectDir(blocker.string());
        std::string bwhere, berr;
        CHECK(!oked::BuildUIGalleryExample(bad, false, &bwhere, &berr));
        CHECK(!berr.empty() && bwhere.empty() && bad.scene().Find("UI Gallery") != nullptr);
    }
    TEST_MAIN_RESULT();
}
