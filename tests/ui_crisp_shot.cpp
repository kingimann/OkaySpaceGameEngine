// Headless picture of the crisp-UI scene (tests/ui_crisp_scene.hpp) through the same UI pass the
// game uses, on SDL's software renderer. Usage: ui_crisp_shot <out.png> [zoom 1..6] [smooth|pixel]
// [nearest]. "nearest" renders at 1x and magnifies the picture with nearest-neighbour: what a
// bitmap-scaled UI looks like. Set OKAY_UI_FILTER=pixel for the old hard-edged drawing.
#define SDL_MAIN_HANDLED
#include "ui_crisp_render.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace okay;

int main(int argc, char** argv) {
    // ui_crisp_shot --save-scene <dir>: write examples/ui_crisp (the scene + its picture) for File > Open Scene.
    if (argc > 2 && std::string(argv[1]) == "--save-scene") {
        const std::string dir = argv[2];
        if (!ui_crisp::WriteTexture(dir + "/crisp_icon.png")) { std::cerr << "cannot write the picture\n"; return 1; }
        Scene scene("ui_crisp");
        ui_crisp::Build(scene, "crisp_icon.png", false);
        std::string err;
        if (!SceneSerializer::SaveToFile(scene, dir + "/ui_crisp.okayscene", &err)) { std::cerr << err << "\n"; return 1; }
        std::cout << "wrote " << dir << "/ui_crisp.okayscene\n";
        return 0;
    }
    if (argc < 2 || !argv[1][0]) { std::cerr << "usage: ui_crisp_shot <out.png> [zoom] [smooth|pixel] [nearest]\n"; return 2; }
    const int zoom = argc > 2 ? std::max(1, std::min(6, std::atoi(argv[2]))) : 1;
    const bool pixel = argc > 3 && std::string(argv[3]) == "pixel";
    const bool nearest = argc > 4 && std::string(argv[4]) == "nearest";
    const bool batched = std::getenv("UI_CRISP_BATCHED") != nullptr;
    const std::string out = argv[1];
    const std::string tex = out + ".tex.png";
    ui_crisp::WriteTexture(tex);
    Image img;
    if (nearest) {
        Image small;
        if (!ui_crisp::Render(ui_crisp::kRefW, ui_crisp::kRefH, tex, pixel, batched, small)) return 1;
        img = Image(ui_crisp::kRefW * zoom, ui_crisp::kRefH * zoom);
        for (int y = 0; y < img.Height(); ++y)
            for (int x = 0; x < img.Width(); ++x) img.SetPixel(x, y, small.GetPixel(x / zoom, y / zoom));
    } else if (!ui_crisp::Render(ui_crisp::kRefW * zoom, ui_crisp::kRefH * zoom, tex, pixel, batched, img)) {
        std::cerr << "no software renderer\n"; return 1;
    }
    std::remove(tex.c_str());
    if (!img.SavePNG(out)) { std::cerr << "failed to write " << out << "\n"; return 1; }
    std::cout << "wrote " << out << " " << img.Width() << "x" << img.Height() << "\n";
    return 0;
}
