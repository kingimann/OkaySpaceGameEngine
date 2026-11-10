#include "ExampleScenes.hpp"
#include "okay/Scene/UIGallery.hpp"
#include <filesystem>

using namespace okay;

namespace oked {

bool BuildUIGalleryExample(okay::editor::EditorState& ed, bool portrait, std::string* savedPath, std::string* error) {
    if (savedPath) savedPath->clear();
    ed.NewScene2D();
    ed.PushUndo();
    GameObject* root = UIGallery::BuildGallery(ed.scene(), (int)UITheme::Preset::ModernFlat, portrait);
    ed.Select(root);
    if (ed.projectDir().empty()) { ed.dirty = false; return true; }   // nowhere to save it: an example is not unsaved work
    const std::string dir = ed.projectDir() + "/Assets/Scenes";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) { if (error) *error = "could not create " + dir; return false; }
    const std::string base = portrait ? "UI Gallery (phone)" : "UI Gallery";
    std::string path = dir + "/" + base + ".okayscene";
    for (int n = 2; std::filesystem::exists(path, ec) && n < 1000; ++n)
        path = dir + "/" + base + " " + std::to_string(n) + ".okayscene";
    if (!ed.Save(path, error)) return false;
    if (savedPath) *savedPath = path;
    return true;
}

} // namespace oked
