// Picture tool (not a test): draws the Controls screen on the typed input system with the game's own UI pass on the software
// painter (no window, no GPU) and writes controls-screen-<size>.png into the folder given as the first argument or $OKAY_SHOT_DIR.
// Nothing is written without one: pictures are not kept in the repository.
#include <Okay.hpp>
#include "okay/Scene/UIGallery.hpp"
#include "ui_soft_render.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

using namespace okay;

static bool Shot(const std::string& path, int w, int h) {
    InputRuntime::Reset();
    Scene scene("controls"); scene.physicsEnabled = false;
    UIGallery::AddControlsScreen(scene);
    scene.Start();
    Input::FeedKeys({}); InputActions::Tick(0.016f); scene.Update(0.016f);
    // one field waits for a key, one has been rebound
    InputRuntime::Set(0)->Rebind("Sprint", 0, 0, "key:v");
    if (GameObject* g = scene.Find("Key Jump")) if (auto* kb = g->GetComponent<UIKeybindField>()) kb->Listen();
    for (int i = 0; i < 4; ++i) { InputActions::Tick(0.016f); scene.Update(0.016f); }
    UICanvas::Set((float)w, (float)h);
    Image img;
    if (!okaytest::SoftPaint(w, h, 18, 22, 32, [&](okay::platform::Painter& painter) {
            ui2d::UIRenderOptions opt; opt.interactive = false;
            ui2d::RenderSceneUI(scene, painter, w, h, opt);
        }, img)) return false;
    const bool ok = img.SavePNG(path);
    std::printf("wrote %s%s\n", path.c_str(), ok ? "" : " (FAILED)");
    return ok;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 && argv[1][0] ? argv[1] : std::getenv("OKAY_SHOT_DIR");
    if (!dir) { std::printf("give an output folder (argument or OKAY_SHOT_DIR); no picture is kept in the repository\n"); return 0; }
    bool ok = Shot(std::string(dir) + "/controls-screen-1280x720.png", 1280, 720);
    ok = Shot(std::string(dir) + "/controls-screen-1920x1080.png", 1920, 1080) && ok;
    return ok ? 0 : 1;
}
