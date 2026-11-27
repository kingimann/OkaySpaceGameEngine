#pragma once
// What the tests of the OkayUI apps in a rectangle share (okayui_script_ide_app, okayui_visual_script_app): writing and reading a
// file of a made-up project, and the last frame drawn through the CPU painter (OkayUI::soft::FrameDrawer, what a program's window
// does: unlike the plain software rasteriser it draws text in fonts made with AddFontTtf, such as the code's JetBrains Mono).
// One copy for both (rule 19c). Written for this project.
#include "OkayUITestKit.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace okayuitest {

inline void WriteFile(const std::filesystem::path& p, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f << text;
}
inline std::string ReadFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
/// The last frame (OkayUI::GetDrawData) painted on a background of (r, g, b) the size of `sim`'s window.
inline Bitmap Picture(const Sim& sim, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    Bitmap out((int)sim.W, (int)sim.H);
    std::unique_ptr<okay::platform::Painter> p = OkayUI::soft::CreateBitmapPainter((int)sim.W, (int)sim.H);
    if (!p) return out;
    p->SetClipRect(nullptr);
    p->SetBlendMode(okay::platform::BlendMode::None);
    p->SetDrawColor(r, g, b, 255);
    p->Clear();
    {
        OkayUI::soft::FrameDrawer fd(*p);
        fd.Draw(OkayUI::GetDrawData());
    }
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    if (p->ReadPixels(rgba, w, h) && w == out.w && h == out.h) out.rgba = rgba;
    return out;
}

}  // namespace okayuitest
