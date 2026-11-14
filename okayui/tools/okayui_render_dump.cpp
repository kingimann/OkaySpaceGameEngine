// Writes every render-test scene as draw data (.okd) and the software reference picture (.ref.rgba, RGBA8, cleared to
// 12, 14, 20) into a directory: okayui_render_dump <dir> [dpi]. Used to check a backend on a machine that cannot run OkayUI,
// see okayui_render_d3d11_check.cpp and docs/own_ui_toolkit.md.
#include "RenderDump.hpp"
#include "../tests/RenderScenes.hpp"
#include "okay/UI/OkayUIRenderBackends.hpp"
#include "OkayUI_Soft.hpp"
#include "okay/Graphics/TtfFont.hpp"

#include <cstdlib>

namespace R = OkayUI::render;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: okayui_render_dump <dir> [dpi]\n"); return 2; }
    const std::string dir = argv[1];
    const float dpi = argc > 2 ? (float)std::atof(argv[2]) : 1.0f;
    int n = 0;
    for (const auto& s : rscenes::All()) {
        okayuitest::Sim sim; rscenes::Run(s, sim);
        const OkayUI::DrawData d = OkayUI::GetDrawData();
        okd::Frame f;
        f.w = (int)std::lround(s.w * dpi); f.h = (int)std::lround(s.h * dpi); f.dpi = dpi;
        f.v.assign((const OkayUI::DrawVertex*)d.vertices, (const OkayUI::DrawVertex*)d.vertices + d.vertexCount);
        f.idx.assign(d.indices, d.indices + d.indexCount);
        f.cmds.assign(d.cmds, d.cmds + d.cmdCount);
        if (d.atlasTexture) { f.atlasKey = (std::uint64_t)(std::uintptr_t)d.atlasTexture; f.atlasW = d.atlasW; f.atlasH = d.atlasH; f.atlas.assign(d.atlasRgba, d.atlasRgba + (size_t)d.atlasW * d.atlasH * 4); }
        if (s.usesImage) { okd::Image im; im.key = (std::uint64_t)(std::uintptr_t)rscenes::ImageKey(); im.w = 32; im.h = 32; im.rgba = rscenes::TestImage(); f.images.push_back(im); }
        for (const OkayUI::DrawCmd& c : f.cmds)
            if (c.texture && c.texture != d.atlasTexture)
                if (okay::TtfFont* font = OkayUI::FallbackFontOf(c.texture)) {
                    bool have = false; for (const auto& im : f.images) have = have || im.key == (std::uint64_t)(std::uintptr_t)c.texture;
                    if (have || !font->Valid()) continue;
                    okd::Image im; im.key = (std::uint64_t)(std::uintptr_t)c.texture; im.w = font->Atlas().Width(); im.h = font->Atlas().Height();
                    im.rgba.assign(font->Atlas().Data(), font->Atlas().Data() + (size_t)im.w * im.h * 4); f.images.push_back(im);
                }
        // the reference: the software backend, same options as every other backend
        OkayUI::soft::Bitmap bm(f.w, f.h); bm.Clear(12, 14, 20);
        auto be = R::CreateSoftBackend(bm); R::Renderer r(*be);
        for (const auto& im : f.images) r.SetImage((const void*)(std::uintptr_t)im.key, im.w, im.h, im.rgba.data(), true);
        R::FrameInfo fi; fi.fbWidth = f.w; fi.fbHeight = f.h; fi.dpi = dpi;
        r.Render(f.Data(), fi);
        const std::string base = dir + "/" + s.name;
        if (!okd::Write(base + ".okd", f)) { std::fprintf(stderr, "cannot write %s\n", base.c_str()); return 1; }
        if (std::FILE* o = std::fopen((base + ".ref.rgba").c_str(), "wb")) { std::fwrite(bm.rgba.data(), 1, bm.rgba.size(), o); std::fclose(o); } else return 1;
        OkayUI::UseLauncherLook(false);
        ++n;
    }
    std::printf("wrote %d scenes to %s (dpi %g)\n", n, dir.c_str(), (double)dpi);
    return 0;
}
