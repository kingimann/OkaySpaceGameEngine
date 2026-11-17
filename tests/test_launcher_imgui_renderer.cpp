// The launcher's own Dear ImGui renderer (shared/ImGuiOwnRenderer.cpp, OKAY_LAUNCHER_RENDERER=own) against a reference
// software rasterisation written here, independently, from the ImDrawData contract in imgui.h:
//   for every command: clip rectangle (minus DisplayPos, times FramebufferScale), triangles from IdxOffset relative to
//   VtxOffset, a pixel is covered when its centre is inside (top-left rule on ties), colour and uv interpolated
//   barycentrically, texel times vertex colour, source-over blending.
// Scenes: flat and gradient shapes with alpha, nested clip rectangles, a user texture (nearest) with a tint, text
// (ImGui's font atlas, served through ImTextureData requests, sampled linearly), a framebuffer scale of 2 and a
// non-zero DisplayPos. Also: the texture requests (create / destroy) and the key table; two GL painters on two windows
// drawn in turn (the launcher and its chat window: each painter makes its own context current); the glue telling the
// window where ImGui's text caret is (the input method's candidate window); and the OkayUI host fed from okay::platform
// events gives OkayUI the same input as the SDL feed.
//
//   test_launcher_imgui_renderer          the software painter (okayui/soft), headless
//   test_launcher_imgui_renderer --gl     our OpenGL painter on a real window (sdl2 and own backends), under Xvfb;
//                                         exit 77 (skipped) without a display or GL
#include "test_framework.hpp"
#include "ImGuiOwnBackend.hpp"
#include "OkayUI_SoftPainter.hpp"
#include "OkayUIHostPlatform.hpp"
#include "okay/Platform/Painter.hpp"
#include "okay/Platform/Platform.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace plat = okay::platform;
using okay::launchergfx::ImGuiPainterRenderer;
using okay::launchergfx::ImGuiPlatformGlue;

namespace {

// ---- the reference rasteriser ---------------------------------------------------------------------------
struct RefTex { int w = 0, h = 0; bool linear = false; std::vector<std::uint8_t> rgba; };

struct Ref {
    int w, h;
    std::vector<double> px;   // RGBA 0..255, unrounded
    Ref(int w_, int h_, const std::uint8_t clear[4]) : w(w_), h(h_), px((std::size_t)w_ * h_ * 4) {
        for (std::size_t i = 0; i < px.size(); ++i) px[i] = clear[i % 4];
    }
};

void SampleRef(const RefTex& t, double u, double v, double out[4]) {
    auto texel = [&](int x, int y, int c) {
        x = std::max(0, std::min(t.w - 1, x)); y = std::max(0, std::min(t.h - 1, y));
        return (double)t.rgba[((std::size_t)y * t.w + x) * 4 + c];
    };
    if (!t.linear) {
        const int x = (int)std::floor(u * t.w), y = (int)std::floor(v * t.h);
        for (int c = 0; c < 4; ++c) out[c] = texel(x, y, c);
        return;
    }
    // Bilinear between the four nearest texel centres (centres at +0.5), clamped at the edges.
    const double fx = u * t.w - 0.5, fy = v * t.h - 0.5;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const double ax = fx - x0, ay = fy - y0;
    for (int c = 0; c < 4; ++c) {
        const double top = texel(x0, y0, c) * (1 - ax) + texel(x0 + 1, y0, c) * ax;
        const double bot = texel(x0, y0 + 1, c) * (1 - ax) + texel(x0 + 1, y0 + 1, c) * ax;
        out[c] = top * (1 - ay) + bot * ay;
    }
}

struct RV { double x, y, u, v, c[4]; };

void TriangleRef(Ref& R, int cx0, int cy0, int cx1, int cy1, const RefTex* tex, RV a, RV b, RV c) {
    double area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (area == 0) return;
    if (area < 0) { std::swap(b, c); area = -area; }
    // Edge function of p -> q at (x, y); positive inside for this winding.
    auto edge = [](const RV& p, const RV& q, double x, double y) { return (p.y - q.y) * x + (q.x - p.x) * y + (p.x * q.y - p.y * q.x); };
    auto topLeft = [](const RV& p, const RV& q) { const double dy = p.y - q.y, dx = q.x - p.x; return dy > 0 || (dy == 0 && dx < 0); };
    const bool tl0 = topLeft(b, c), tl1 = topLeft(c, a), tl2 = topLeft(a, b);
    const int x0 = std::max(cx0, (int)std::floor(std::min({a.x, b.x, c.x}))), x1 = std::min(cx1, (int)std::ceil(std::max({a.x, b.x, c.x})));
    const int y0 = std::max(cy0, (int)std::floor(std::min({a.y, b.y, c.y}))), y1 = std::min(cy1, (int)std::ceil(std::max({a.y, b.y, c.y})));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const double w0 = edge(b, c, px, py), w1 = edge(c, a, px, py), w2 = edge(a, b, px, py);
            if (!(w0 > 0 || (w0 == 0 && tl0)) || !(w1 > 0 || (w1 == 0 && tl1)) || !(w2 > 0 || (w2 == 0 && tl2))) continue;
            const double l0 = w0 / area, l1 = w1 / area, l2 = w2 / area;
            double col[4];
            for (int k = 0; k < 4; ++k) col[k] = l0 * a.c[k] + l1 * b.c[k] + l2 * c.c[k];
            if (tex) {
                double t[4];
                SampleRef(*tex, l0 * a.u + l1 * b.u + l2 * c.u, l0 * a.v + l1 * b.v + l2 * c.v, t);
                for (int k = 0; k < 4; ++k) col[k] = col[k] * t[k] / 255.0;
            }
            double* d = &R.px[((std::size_t)y * R.w + x) * 4];
            const double al = col[3] / 255.0;
            for (int k = 0; k < 3; ++k) d[k] = col[k] * al + d[k] * (1 - al);
            d[3] = col[3] + d[3] * (1 - al);
        }
}

void RenderRef(Ref& R, const ImDrawData* dd, const std::map<ImTextureID, RefTex>& texs) {
    const ImVec2 off = dd->DisplayPos, sc = dd->FramebufferScale;
    for (const ImDrawList* l : dd->CmdLists)
        for (const ImDrawCmd& cmd : l->CmdBuffer) {
            if (cmd.UserCallback || cmd.ElemCount == 0) continue;
            // Pixels whose centre is inside the clip rectangle.
            const double fx0 = (cmd.ClipRect.x - off.x) * sc.x, fy0 = (cmd.ClipRect.y - off.y) * sc.y;
            const double fx1 = (cmd.ClipRect.z - off.x) * sc.x, fy1 = (cmd.ClipRect.w - off.y) * sc.y;
            const int cx0 = std::max(0, (int)std::ceil(fx0 - 0.5)), cy0 = std::max(0, (int)std::ceil(fy0 - 0.5));
            const int cx1 = std::min(R.w, (int)std::ceil(fx1 - 0.5)), cy1 = std::min(R.h, (int)std::ceil(fy1 - 0.5));
            if (cx1 <= cx0 || cy1 <= cy0) continue;
            const RefTex* tex = nullptr;
            auto it = texs.find(cmd.GetTexID());
            if (it != texs.end()) tex = &it->second;
            auto V = [&](unsigned i) {
                const ImDrawVert& s = l->VtxBuffer[(int)(cmd.VtxOffset + i)];
                RV r;
                r.x = (s.pos.x - off.x) * sc.x; r.y = (s.pos.y - off.y) * sc.y; r.u = s.uv.x; r.v = s.uv.y;
                const ImVec4 c = ImGui::ColorConvertU32ToFloat4(s.col);
                r.c[0] = c.x * 255.0; r.c[1] = c.y * 255.0; r.c[2] = c.z * 255.0; r.c[3] = c.w * 255.0;
                for (double& k : r.c) k = std::round(k);
                return r;
            };
            for (unsigned i = 0; i + 3 <= cmd.ElemCount; i += 3) {
                const ImDrawIdx* ix = &l->IdxBuffer[(int)(cmd.IdxOffset + i)];
                TriangleRef(R, cx0, cy0, cx1, cy1, tex, V(ix[0]), V(ix[1]), V(ix[2]));
            }
        }
}

// ---- comparison --------------------------------------------------------------------------------------------
struct Diff { int bad = 0, maxd = 0; long long total = 0; };
Diff Compare(const std::vector<std::uint8_t>& got, const Ref& R, int tol) {
    Diff d;
    for (int i = 0; i < R.w * R.h; ++i) {
        int m = 0;
        for (int k = 0; k < 3; ++k) {   // colour channels (the window's alpha channel is not shown)
            const int ref = (int)std::lround(std::max(0.0, std::min(255.0, R.px[(std::size_t)i * 4 + k])));
            m = std::max(m, std::abs(ref - (int)got[(std::size_t)i * 4 + k]));
        }
        d.maxd = std::max(d.maxd, m);
        d.total += m;
        if (m > tol) ++d.bad;
    }
    return d;
}

// ---- scenes -------------------------------------------------------------------------------------------------
using Scene = std::function<void(ImDrawList*, ImTextureID user)>;

struct Case { const char* name; Scene draw; float scale; ImVec2 displayPos; };

const int kW = 320, kH = 240;

std::vector<Case> Cases() {
    std::vector<Case> c;
    c.push_back({"flat shapes and alpha", [](ImDrawList* dl, ImTextureID) {
        dl->AddRectFilled(ImVec2(10, 10), ImVec2(150, 90), IM_COL32(200, 40, 40, 255));
        dl->AddRectFilled(ImVec2(80, 50), ImVec2(220, 140), IM_COL32(40, 200, 60, 128));
        dl->AddTriangleFilled(ImVec2(240, 20), ImVec2(310, 200), ImVec2(170, 180), IM_COL32(60, 80, 230, 200));
        dl->AddRectFilledMultiColor(ImVec2(20, 150), ImVec2(160, 230), IM_COL32(255, 0, 0, 255), IM_COL32(0, 255, 0, 255),
                                    IM_COL32(0, 0, 255, 128), IM_COL32(255, 255, 0, 64));
    }, 1.0f, ImVec2(0, 0)});
    c.push_back({"clip rectangles", [](ImDrawList* dl, ImTextureID) {
        dl->PushClipRect(ImVec2(30, 30), ImVec2(200, 120), false);
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(320, 240), IM_COL32(220, 180, 40, 255));
        dl->PushClipRect(ImVec2(60, 50), ImVec2(260, 200), true);   // intersected with the outer one
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(320, 240), IM_COL32(20, 90, 200, 160));
        dl->PopClipRect();
        dl->PopClipRect();
        dl->PushClipRect(ImVec2(250.5f, 150.25f), ImVec2(300.75f, 230.5f), false);   // fractional edges
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(320, 240), IM_COL32(250, 250, 250, 255));
        dl->PopClipRect();
    }, 1.0f, ImVec2(0, 0)});
    c.push_back({"user texture, tint and alpha", [](ImDrawList* dl, ImTextureID user) {
        dl->AddImage(user, ImVec2(10, 10), ImVec2(138, 138));
        // Sizes that are whole multiples of the texel count: no pixel centre falls exactly on a texel edge, where a GPU may
        // pick either neighbour.
        dl->AddImage(user, ImVec2(150, 20), ImVec2(310, 100), ImVec2(0.25f, 0.25f), ImVec2(0.75f, 0.75f), IM_COL32(255, 128, 64, 255));
        dl->AddImage(user, ImVec2(60, 100), ImVec2(252, 228), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 110));
    }, 1.0f, ImVec2(0, 0)});
    c.push_back({"text (font atlas, linear)", [](ImDrawList* dl, ImTextureID) {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(320, 240), IM_COL32(30, 30, 32, 255));
        dl->AddText(ImVec2(12, 12), IM_COL32(255, 255, 255, 255), "Create  Play  Marketplace");
        dl->AddText(ImVec2(12, 40), IM_COL32(120, 200, 255, 255), "The quick brown fox 0123456789");
        dl->AddText(nullptr, 26.0f, ImVec2(12, 80), IM_COL32(255, 210, 90, 220), "OkaySpace");
        dl->AddCircleFilled(ImVec2(250, 170), 50.0f, IM_COL32(90, 220, 140, 180));
    }, 1.0f, ImVec2(0, 0)});
    c.push_back({"framebuffer scale 2 and DisplayPos", [](ImDrawList* dl, ImTextureID user) {
        dl->AddRectFilled(ImVec2(105, 55), ImVec2(170, 100), IM_COL32(200, 60, 160, 255));
        dl->PushClipRect(ImVec2(140, 60), ImVec2(240, 160), false);
        dl->AddImage(user, ImVec2(120, 70), ImVec2(200, 150), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 200));
        dl->PopClipRect();
        dl->AddText(ImVec2(110, 130), IM_COL32(255, 255, 255, 255), "hi-dpi");
    }, 2.0f, ImVec2(100, 50)});
    return c;
}

std::vector<std::uint8_t> Checker(int n) {
    std::vector<std::uint8_t> px((std::size_t)n * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            std::uint8_t* p = &px[((std::size_t)y * n + x) * 4];
            const bool on = ((x / 4) + (y / 4)) % 2 == 0;
            p[0] = on ? 240 : (std::uint8_t)(x * 8); p[1] = on ? 240 : 30; p[2] = on ? 60 : (std::uint8_t)(y * 8);
            p[3] = (x + y) % 7 == 0 ? 128 : 255;
        }
    return px;
}

// Run every case on `p` and compare with the reference. `tol` = largest per-channel difference that counts as equal,
// `maxBadFrac` = the share of pixels allowed past it (GPU edge and sampling precision).
void RunCases(plat::Painter& p, const char* what, int tol, double maxBadFrac, const std::function<void()>& afterPresent = nullptr) {
    RUN_SUITE(std::string("ImGui renderer on the ") + what + " painter");
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    const std::vector<std::uint8_t> chk = Checker(32);
    plat::TextureDesc td; td.width = 32; td.height = 32; td.scale = plat::ScaleMode::Nearest;
    okay::ui2d::Tex user = p.CreateTexture(td, chk.data());
    CHECK(user != nullptr);
    {
        ImGuiPainterRenderer r(p);
        CHECK((io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0);
        const std::vector<Case> cases = Cases();
        for (std::size_t ci = 0; ci < cases.size(); ++ci) {
            const Case& c = cases[ci];
            const float s = c.scale;
            io.DisplaySize = ImVec2(kW / s, kH / s);
            io.DisplayFramebufferScale = ImVec2(s, s);
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            // DisplayPos: draw in a viewport that starts at displayPos (shift what the scene drew by it).
            const ImTextureID uid = (ImTextureID)(std::uintptr_t)user;
            c.draw(dl, uid);
            ImGui::Render();
            ImDrawData* dd = ImGui::GetDrawData();
            dd->DisplayPos = c.displayPos;
            dd->DisplaySize = ImVec2(kW / s, kH / s);
            if (c.displayPos.x != 0 || c.displayPos.y != 0) {
                // The scene was written in viewport coordinates starting at displayPos already; ImGui's own clip rectangle of
                // the foreground list starts at (0,0), so move it with the viewport.
                for (ImDrawList* l : dd->CmdLists)
                    for (ImDrawCmd& cmd : l->CmdBuffer)
                        if (cmd.ClipRect.x == 0 && cmd.ClipRect.y == 0) { cmd.ClipRect.x += c.displayPos.x; cmd.ClipRect.y += c.displayPos.y; cmd.ClipRect.z += c.displayPos.x; cmd.ClipRect.w += c.displayPos.y; }
            }
            const std::uint8_t clear[4] = {30, 30, 32, 255};
            p.SetClipRect(nullptr);
            p.SetDrawColor(clear[0], clear[1], clear[2], clear[3]);
            p.Clear();
            r.Render(dd);   // also serves the font atlas requests
            std::vector<std::uint8_t> got; int gw = 0, gh = 0;
            CHECK(p.ReadPixels(got, gw, gh));
            if (afterPresent) afterPresent();
            CHECK(gw == kW && gh == kH);
            if (gw != kW || gh != kH) continue;
            // The reference textures: ours, and ImGui's own CPU copy of every texture it asked for.
            std::map<ImTextureID, RefTex> texs;
            texs[uid] = RefTex{32, 32, false, chk};
            for (ImTextureData* t : *dd->Textures) {
                CHECK(t->Status == ImTextureStatus_OK);
                CHECK(t->TexID != ImTextureID_Invalid);
                RefTex rt; rt.w = t->Width; rt.h = t->Height; rt.linear = true;
                rt.rgba.resize((std::size_t)t->Width * t->Height * 4);
                for (int i = 0; i < t->Width * t->Height; ++i) {
                    if (t->Format == ImTextureFormat_RGBA32) std::memcpy(&rt.rgba[(std::size_t)i * 4], t->Pixels + (std::size_t)i * 4, 4);
                    else { rt.rgba[(std::size_t)i * 4] = rt.rgba[(std::size_t)i * 4 + 1] = rt.rgba[(std::size_t)i * 4 + 2] = 255; rt.rgba[(std::size_t)i * 4 + 3] = t->Pixels[i]; }
                }
                texs[t->TexID] = rt;
            }
            Ref R(kW, kH, clear);
            RenderRef(R, dd, texs);
            const Diff d = Compare(got, R, tol);
            const double frac = (double)d.bad / (kW * kH);
            std::printf("  %-40s commands %d, pixels off by more than %d: %d (%.3f%%), largest difference %d, mean %.4f\n",
                        c.name, r.LastCommands(), tol, d.bad, frac * 100.0, d.maxd, (double)d.total / (kW * kH));
            CHECK(r.LastCommands() > 0);
            CHECK(frac <= maxBadFrac);
            if (std::getenv("OKAY_TEST_DUMP")) {   // PPM pictures to look at: <name>-got.ppm / -ref.ppm
                std::string tag = what;
                for (char& ch : tag) if (!std::isalnum((unsigned char)ch)) ch = '_';
                std::string base = std::string(std::getenv("OKAY_TEST_DUMP")) + "/" + tag + "-" + std::to_string(ci);
                for (int which = 0; which < 2; ++which) {
                    FILE* f = std::fopen((base + (which ? "-ref.ppm" : "-got.ppm")).c_str(), "wb");
                    if (!f) break;
                    std::fprintf(f, "P6 %d %d 255\n", kW, kH);
                    for (int i = 0; i < kW * kH; ++i)
                        for (int k = 0; k < 3; ++k) {
                            const int v = which ? (int)std::lround(std::max(0.0, std::min(255.0, R.px[(std::size_t)i * 4 + k]))) : got[(std::size_t)i * 4 + k];
                            std::fputc(v, f);
                        }
                    std::fclose(f);
                }
            }
        }
        // The renderer's textures go with it.
    }
    for (ImTextureData* t : ImGui::GetPlatformIO().Textures) CHECK(t->TexID == ImTextureID_Invalid);
    CHECK(io.BackendRendererName == nullptr);
    p.DestroyTexture(user);
    ImGui::DestroyContext(ctx);
}

void KeyTable() {
    RUN_SUITE("our keys to ImGui keys");
    using K = plat::Key;
    CHECK(okay::launchergfx::ImGuiKeyFor(K::A) == ImGuiKey_A);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::Z) == ImGuiKey_Z);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::N1) == ImGuiKey_1);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::N0) == ImGuiKey_0);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::F1) == ImGuiKey_F1);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::F12) == ImGuiKey_F12);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::Return) == ImGuiKey_Enter);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::Backspace) == ImGuiKey_Backspace);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::Left) == ImGuiKey_LeftArrow);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::Up) == ImGuiKey_UpArrow);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::Kp0) == ImGuiKey_Keypad0);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::Kp9) == ImGuiKey_Keypad9);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::KpEnter) == ImGuiKey_KeypadEnter);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::LCtrl) == ImGuiKey_LeftCtrl);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::RGui) == ImGuiKey_RightSuper);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::Grave) == ImGuiKey_GraveAccent);
    CHECK(okay::launchergfx::ImGuiKeyFor(K::Unknown) == ImGuiKey_None);
    // Every named key of ours maps to a distinct ImGui key.
    std::map<int, int> seen;
    for (int k = 1; k < 256; ++k) {
        const int g = okay::launchergfx::ImGuiKeyFor((K)k);
        if (g == ImGuiKey_None) continue;
        CHECK(seen.count(g) == 0);
        seen[g] = k;
    }
    CHECK(seen.size() >= 100);
}

#ifdef OKAY_PLATFORM_HAVE_SDL2   // compares with the host's SDL feed; a build with -DOKAY_WITH_SDL=OFF has none
// The OkayUI host fed from okay::platform events (the own renderer) builds the same OkayUI input as the SDL feed (the sdl
// renderer) for the same keys, text, clicks and wheel.
void OkayUIHostFeeds() {
    RUN_SUITE("OkayUI host: okay::platform events give the same input as SDL events");
    using ET = plat::EventType;
    okayuihost::InputCollector viaSdl, viaPlat;
    auto both = [&](const SDL_Event& se, const plat::Event& pe) { viaSdl.Feed(se); okayuihost::FeedPlatform(viaPlat, pe); };
    SDL_Event se; plat::Event pe;
    auto key = [&](SDL_Keycode sym, plat::Key k, bool down, Uint16 smod, std::uint32_t pmod) {
        SDL_memset(&se, 0, sizeof(se)); se.type = down ? SDL_KEYDOWN : SDL_KEYUP; se.key.keysym.sym = sym; se.key.keysym.mod = smod;
        pe = plat::Event(); pe.type = down ? ET::KeyDown : ET::KeyUp; pe.key = k; pe.mods = pmod;
        both(se, pe);
    };
    auto compare = [&](const char* what) {
        const OkayUI::Input a = viaSdl.Build(10, 20, false, false, false, true);
        const OkayUI::Input b = viaPlat.Build(10, 20, false, false, false, true);
        const std::string ta = a.text ? a.text : "", tb = b.text ? b.text : "";
        bool same = ta == tb && a.mouseDown == b.mouseDown && a.rightDown == b.rightDown && a.wheel == b.wheel &&
                    a.backspace == b.backspace && a.del == b.del && a.left == b.left && a.right == b.right && a.home == b.home &&
                    a.end == b.end && a.enter == b.enter && a.escape == b.escape && a.copy == b.copy && a.cut == b.cut &&
                    a.paste == b.paste && a.up == b.up && a.down == b.down && a.selectAll == b.selectAll && a.shift == b.shift &&
                    a.ctrl == b.ctrl;
        for (int k = 0; k < OkayUI::KeyId_COUNT; ++k) same = same && a.keyDown[k] == b.keyDown[k];
        std::printf("  %-44s %s (text \"%s\")\n", what, same ? "same" : "DIFFERENT", tb.c_str());
        CHECK(same);
        return b;
    };
    // typed text, a left click, two wheel notches up
    SDL_memset(&se, 0, sizeof(se)); se.type = SDL_TEXTINPUT; std::strcpy(se.text.text, "h\xC3\xA9");
    pe = plat::Event(); pe.type = ET::TextInput; pe.text = "h\xC3\xA9";
    both(se, pe);
    SDL_memset(&se, 0, sizeof(se)); se.type = SDL_MOUSEBUTTONDOWN; se.button.button = SDL_BUTTON_LEFT;
    pe = plat::Event(); pe.type = ET::MouseButtonDown; pe.button = plat::MouseLeft;
    both(se, pe);
    SDL_memset(&se, 0, sizeof(se)); se.type = SDL_MOUSEWHEEL; se.wheel.y = 2; se.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    pe = plat::Event(); pe.type = ET::MouseWheel; pe.wheelY = 2.0f;
    both(se, pe);
    OkayUI::Input in = compare("text, click and wheel");
    CHECK(in.text && std::strcmp(in.text, "h\xC3\xA9") == 0 && in.mouseDown && in.wheel == 2.0f);
    // editing keys and shortcuts
    key(SDLK_BACKSPACE, plat::Key::Backspace, true, 0, 0);
    key(SDLK_a, plat::Key::A, true, KMOD_LCTRL, plat::ModCtrl);
    key(SDLK_LEFT, plat::Key::Left, true, KMOD_LSHIFT, plat::ModShift);
    in = compare("Backspace, Ctrl+A, Shift+Left");
    CHECK(in.backspace && in.selectAll && in.left && in.shift && in.keyDown[OkayUI::KeyId_Left]);
    key(SDLK_LEFT, plat::Key::Left, false, 0, 0);
    key(SDLK_c, plat::Key::C, true, KMOD_LCTRL, plat::ModCtrl);
    key(SDLK_RETURN, plat::Key::Return, true, 0, 0);
    key(SDLK_KP_ENTER, plat::Key::KpEnter, true, 0, 0);
    key(SDLK_ESCAPE, plat::Key::Escape, true, 0, 0);
    key(SDLK_DELETE, plat::Key::Delete, true, 0, 0);
    key(SDLK_HOME, plat::Key::Home, true, 0, 0);
    key(SDLK_END, plat::Key::End, true, 0, 0);
    key(SDLK_UP, plat::Key::Up, true, 0, 0);
    key(SDLK_DOWN, plat::Key::Down, true, 0, 0);
    key(SDLK_v, plat::Key::V, true, KMOD_LGUI, plat::ModGui);
    key(SDLK_x, plat::Key::X, true, KMOD_RCTRL, plat::ModCtrl);
    key(SDLK_F5, plat::Key::F5, true, 0, 0);
    key(SDLK_7, plat::Key::N7, true, 0, 0);
    in = compare("Ctrl+C, Enter, Escape, Delete, ... F5, 7");
    CHECK(in.copy && in.enter && in.escape && in.del && in.home && in.end && in.up && in.down && in.paste && in.cut);
    CHECK(!in.keyDown[OkayUI::KeyId_Left] && in.keyDown[OkayUI::KeyId_F5] && in.keyDown[OkayUI::KeyId_7]);
    // the key table itself
    CHECK(okayuihost::KeyIdOfPlatform(plat::Key::Tab) == OkayUI::KeyId_Tab);
    CHECK(okayuihost::KeyIdOfPlatform(plat::Key::RShift) == OkayUI::KeyId_Shift);
    CHECK(okayuihost::KeyIdOfPlatform(plat::Key::N0) == OkayUI::KeyId_0);
    CHECK(okayuihost::KeyIdOfPlatform(plat::Key::Unknown) == OkayUI::KeyId_None);
    CHECK(okayuihost::KeyIdOfPlatform(plat::Key::F12) == OkayUI::KeyId_F12);
}
#endif

// The glue tells the window where ImGui's text caret is while a text field has the keyboard (Platform_SetImeDataFn ->
// Window::SetTextInputArea): the input method's candidate window opens there.
void ImeCaret(plat::Backend& b, plat::Window& w, plat::Painter& p) {
    RUN_SUITE(std::string("the IME caret on a ") + b.Name() + " window");
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().Fonts->AddFontDefault();
    {
        ImGuiPainterRenderer r(p);
        ImGuiPlatformGlue g(b, w);
        CHECK(ImGui::GetPlatformIO().Platform_SetImeDataFn != nullptr);
        int x = 0, y = 0, h = 0;
        CHECK(!g.LastImeArea(x, y, h));
        static char buf[64] = "abc";
        for (int f = 0; f < 4; ++f) {
            g.NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(24, 30));
            ImGui::SetNextWindowSize(ImVec2(200, 80));
            ImGui::Begin("ime", nullptr, ImGuiWindowFlags_NoDecoration);
            if (f == 0) ImGui::SetKeyboardFocusHere();
            ImGui::InputText("##text", buf, sizeof(buf));
            ImGui::End();
            ImGui::Render();
            p.SetDrawColor(0, 0, 0, 255); p.Clear();
            r.Render(ImGui::GetDrawData());
            p.Present();
        }
        bool taken = false;
        CHECK(g.LastImeArea(x, y, h, &taken));
        std::printf("  caret at %d,%d, line height %d: %s\n", x, y, h, taken ? "taken by the window" : "not taken (no over-the-spot input method)");
        CHECK(x >= 24 && x < 224 && y >= 30 && y < 110 && h >= 10 && h <= 30);
        CHECK(ImGui::GetIO().WantTextInput);
        if (b.Kind() == plat::BackendKind::Sdl2) CHECK(taken);
    }
    CHECK(ImGui::GetPlatformIO().Platform_SetImeDataFn == nullptr);
    ImGui::DestroyContext(ctx);
}

int RunGl() {
    if (!std::getenv("DISPLAY")) { std::printf("SKIP: no $DISPLAY (run through tests/run_under_xvfb.sh)\n"); return 77; }
    int ran = 0;
    for (plat::BackendKind kind : {plat::BackendKind::Sdl2, plat::BackendKind::Own}) {
        plat::Backend* b = plat::Get(kind);
        if (!b) continue;
        plat::InitFlags f; f.audio = false; f.gamepad = false;
        if (!b->Init(f)) { std::printf("  %s backend: no display (%s)\n", b->Name(), b->LastError().c_str()); continue; }
        plat::WindowDesc d;
        d.title = "imgui renderer test"; d.width = kW; d.height = kH; d.opengl = true; d.resizable = false;
        std::unique_ptr<plat::Window> w = b->MakeWindow(d);
        if (!w) { std::printf("  %s backend: no GL window (%s)\n", b->Name(), b->LastError().c_str()); continue; }
        std::string err;
        std::unique_ptr<plat::Painter> p = plat::CreateGLPainter(*w, *b, plat::PainterDesc(), &err);
        if (!p) { std::printf("  %s backend: no GL painter (%s)\n", b->Name(), err.c_str()); continue; }
        CHECK(p->Kind() == plat::PainterKind::OpenGL && std::strcmp(p->Name(), "opengl") == 0);
        for (int i = 0; i < 10; ++i) { plat::Event e; while (b->PollEvent(e)) {} b->DelayMs(10); }
        // Every cursor shape ImGui asks for exists on this backend (SDL system cursors / the X core cursor font), and the arrow comes back.
        for (int c = (int)plat::SystemCursor::Arrow; c <= (int)plat::SystemCursor::Wait; ++c) CHECK(b->SetSystemCursor((plat::SystemCursor)c));
        CHECK(b->SetSystemCursor(plat::SystemCursor::Arrow));
        int ow = 0, oh = 0;
        p->OutputSize(&ow, &oh);
        CHECK(ow == kW && oh == kH);
        // GPU rasterisation keeps 8 bits of sub-pixel precision and blends in 8 bits: allow small differences on a few
        // edge pixels, nothing more.
        RunCases(*p, (std::string("opengl (") + b->Name() + " window)").c_str(), 6, 0.001, [&] { p->Present(); });
        ImeCaret(*b, *w, *p);
        {   // Two windows, two GL contexts (the launcher and its chat window). The second painter was made last, so its context
            // is current: every call on the first one must make the first context current again, and the other way round.
            RUN_SUITE(std::string("two GL painters on two ") + b->Name() + " windows");
            plat::WindowDesc d2 = d;
            d2.title = "imgui renderer test, second window";
            std::unique_ptr<plat::Window> w2 = b->MakeWindow(d2);
            CHECK(w2 != nullptr);
            std::unique_ptr<plat::Painter> p2 = w2 ? plat::CreateGLPainter(*w2, *b, plat::PainterDesc(), &err) : nullptr;
            CHECK(p2 != nullptr);
            if (p2) {
                for (int i = 0; i < 5; ++i) { plat::Event e; while (b->PollEvent(e)) {} b->DelayMs(10); }
                RunCases(*p, (std::string("opengl first of two (") + b->Name() + ")").c_str(), 6, 0.001, [&] { p->Present(); });
                RunCases(*p2, (std::string("opengl second of two (") + b->Name() + ")").c_str(), 6, 0.001, [&] { p2->Present(); });
                // In turn: a texture on each, a clear on each, a textured quad on each, then read both back.
                std::vector<std::uint8_t> red(16 * 16 * 4), blue(16 * 16 * 4);
                for (int i = 0; i < 16 * 16; ++i) { red[i * 4] = 220; red[i * 4 + 3] = 255; blue[i * 4 + 2] = 220; blue[i * 4 + 3] = 255; }
                plat::TextureDesc td; td.width = 16; td.height = 16;
                okay::ui2d::Tex t1 = p->CreateTexture(td, red.data());
                okay::ui2d::Tex t2 = p2->CreateTexture(td, blue.data());
                CHECK(t1 && t2);
                p->SetClipRect(nullptr); p2->SetClipRect(nullptr);
                p->SetDrawColor(10, 200, 10, 255); p->Clear();
                p2->SetDrawColor(200, 200, 10, 255); p2->Clear();
                const okay::ui2d::Rect dst{8, 8, 32, 32};
                p->Copy(t1, nullptr, &dst);
                p2->Copy(t2, nullptr, &dst);
                std::vector<std::uint8_t> g1, g2; int w1 = 0, h1 = 0, wv2 = 0, hv2 = 0;
                CHECK(p2->ReadPixels(g2, wv2, hv2));
                CHECK(p->ReadPixels(g1, w1, h1));
                auto px = [](const std::vector<std::uint8_t>& v, int width, int x, int y) { return &v[((std::size_t)y * width + x) * 4]; };
                if (w1 == kW && wv2 == kW && !g1.empty() && !g2.empty()) {
                    const std::uint8_t* a0 = px(g1, w1, 2, 2); const std::uint8_t* a1 = px(g1, w1, 20, 20);
                    const std::uint8_t* b0 = px(g2, wv2, 2, 2); const std::uint8_t* b1 = px(g2, wv2, 20, 20);
                    std::printf("  first: clear %d,%d,%d quad %d,%d,%d   second: clear %d,%d,%d quad %d,%d,%d\n",
                                a0[0], a0[1], a0[2], a1[0], a1[1], a1[2], b0[0], b0[1], b0[2], b1[0], b1[1], b1[2]);
                    CHECK(a0[0] == 10 && a0[1] == 200 && a0[2] == 10);
                    CHECK(a1[0] == 220 && a1[1] == 0 && a1[2] == 0);
                    CHECK(b0[0] == 200 && b0[1] == 200 && b0[2] == 10);
                    CHECK(b1[0] == 0 && b1[1] == 0 && b1[2] == 220);
                }
                p->Present(); p2->Present();
                p->DestroyTexture(t1); p2->DestroyTexture(t2);
            }
            p2.reset();
            w2.reset();
        }
        p.reset();
        w.reset();
        ++ran;
    }
    if (!ran) { std::printf("SKIP: no backend could make a GL window\n"); return 77; }
    TEST_MAIN_RESULT();
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--gl") == 0) return RunGl();
    KeyTable();
#ifdef OKAY_PLATFORM_HAVE_SDL2
    OkayUIHostFeeds();
#else
    std::printf("(no SDL in this build: the OkayUI host SDL-vs-platform comparison is skipped)\n");
#endif
    std::unique_ptr<plat::Painter> soft = OkayUI::soft::CreateBitmapPainter(kW, kH);
    CHECK(soft != nullptr);
    if (soft) RunCases(*soft, "software", 2, 0.0);
    TEST_MAIN_RESULT();
}
