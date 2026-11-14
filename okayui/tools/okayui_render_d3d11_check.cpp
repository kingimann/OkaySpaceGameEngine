// Draws the frames written by okayui_render_dump with the Direct3D 11 render backend and compares them with the software
// reference: okayui_render_d3d11_check <dir> [warp|hardware|reference] [write]. Windows only (also runs under Wine). Draws into an
// RGBA8 render target, reads it back with the backend's ReadPixels, prints per scene the largest and mean channel difference
// and the number of pixels more than 8 apart, and returns non-zero when a frame is refused or the mean difference is above 0.5.
#if defined(_WIN32)
#include "RenderDump.hpp"
#include "okay/UI/OkayUIRenderBackends.hpp"

#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace R = OkayUI::render;

static bool ReadFile(const std::string& p, std::vector<std::uint8_t>& out) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    const bool ok = n == 0 || std::fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    std::fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: okayui_render_d3d11_check <dir> [warp|hardware|reference] [write]\n"); return 2; }
    const std::string dir = argv[1];
    const std::string kind = argc > 2 ? argv[2] : "hardware";
    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; D3D_FEATURE_LEVEL fl;
    const D3D_DRIVER_TYPE dt = kind == "warp" ? D3D_DRIVER_TYPE_WARP : kind == "reference" ? D3D_DRIVER_TYPE_REFERENCE : D3D_DRIVER_TYPE_HARDWARE;
    if (FAILED(D3D11CreateDevice(nullptr, dt, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) { std::puts("no Direct3D 11 device"); return 2; }
    std::string err;
    auto be = R::CreateD3D11Backend(dev, ctx, &err);
    if (!be) { std::printf("backend not created: %s\n", err.c_str()); return 2; }
    std::puts("scene                          size       max   mean     >8   differing");
    int bad = 0, n = 0;
    const char* names[] = { "primitives", "demo_window", "lab", "colorpicker", "table_scroll", "plots", "stacking", "nested_clip", "images", "shapes", "code_view" };
    std::vector<std::string> scenes;
    for (const char* s : names) { scenes.push_back(s); scenes.push_back(std::string(s) + "_launcher"); }
    scenes.push_back("fallback_text_launcher");
    for (const std::string& name : scenes) {
        okd::Frame fr; std::vector<std::uint8_t> ref;
        if (!okd::Read(dir + "/" + name + ".okd", fr) || !ReadFile(dir + "/" + name + ".ref.rgba", ref) || ref.size() != (size_t)fr.w * fr.h * 4) { std::printf("%-28s cannot read\n", name.c_str()); ++bad; continue; }
        // an RGBA8 render target of the frame size, cleared like the reference
        D3D11_TEXTURE2D_DESC td = {}; td.Width = (UINT)fr.w; td.Height = (UINT)fr.h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
        ID3D11Texture2D* rt = nullptr; ID3D11RenderTargetView* rtv = nullptr;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &rt)) || FAILED(dev->CreateRenderTargetView(rt, nullptr, &rtv))) { std::printf("%-28s no render target\n", name.c_str()); ++bad; continue; }
        const float clear[4] = { 12 / 255.0f, 14 / 255.0f, 20 / 255.0f, 1.0f };
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, clear);
        R::Renderer r(*be);
        for (const okd::Image& im : fr.images) r.SetImage((const void*)(std::uintptr_t)im.key, im.w, im.h, im.rgba.data(), true);
        R::FrameInfo fi; fi.fbWidth = fr.w; fi.fbHeight = fr.h; fi.dpi = fr.dpi;
        const R::Stats st = r.Render(fr.Data(), fi);
        std::vector<std::uint8_t> got; int gw = 0, gh = 0;
        const bool rb = be->ReadPixels(got, gw, gh) && gw == fr.w && gh == fr.h;
        if (st.refused || !rb) { std::printf("%-28s refused or no read-back (%s)\n", name.c_str(), st.message.c_str()); ++bad; }
        else {
            int maxd = 0; long long above = 0, differing = 0; double sum = 0;
            for (size_t i = 0; i < got.size(); i += 4) {
                int m = 0;
                for (int c = 0; c < 4; ++c) { const int e = std::abs((int)got[i + c] - (int)ref[i + c]); sum += e; m = std::max(m, e); }
                maxd = std::max(maxd, m); if (m > 8) ++above; if (m > 0) ++differing;
            }
            const double mean = sum / (double)got.size();
            if (argc > 3) if (std::FILE* o = std::fopen((dir + "/" + name + ".out.rgba").c_str(), "wb")) { std::fwrite(got.data(), 1, got.size(), o); std::fclose(o); }   // "write": keep the picture
            std::printf("%-28s %4dx%-4d %5d %7.4f %7lld %9lld\n", name.c_str(), fr.w, fr.h, maxd, mean, above, differing);
            if (mean > 0.5) ++bad;
        }
        r.ReleaseAll();
        rtv->Release(); rt->Release();
        ++n;
    }
    std::printf("%d scenes drawn, %d problems\n", n, bad);
    return bad ? 1 : 0;
}
#else
int main() { return 0; }
#endif
