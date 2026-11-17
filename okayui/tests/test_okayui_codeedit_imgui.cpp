// Our code editor hosted in a Dear ImGui window the way the script editor hosts it (editor/OkayUIInImGui.hpp,
// editor/ScriptEditorWidget.hpp): its geometry goes into the ImGui window's draw list, so an ImGui popup opened over the code
// later in the frame is drawn AFTER the code, on top of it. Before this, OkayUI drew its whole frame after ImGui and covered
// the script editor's autocomplete list and hover help.
//
// Headless: Dear ImGui with its SDL_Renderer backend on SDL's software renderer (a surface, no window), OkayUI in the launcher
// look driven by okayuitest::Sim. Checked: the draw lists' order, the pixels under the popup, that OkayUI's own frame no longer
// holds the code, the texture lookup, and that clicks still reach the editor.
// Step 30: the hover help as an ImGui tooltip (ShowCodeHover: an ImGui window drawn after the code and after an open popup,
// nothing in OkayUI's frame), and the keyboard claimed from ImGui while ours has it (ClaimKeyboardForCode: the arrows do not
// move ImGui's nav cursor, Space and Enter do not press a button, Ctrl+S still reaches the script editor, WantTextInput is set,
// typed characters leave ImGui's queue), with the unclaimed case for contrast.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIInImGui.hpp"
#include "ScriptEditorCodeEdit.hpp"
#include "imgui.h"
#include "imgui_impl_sdlrenderer2.h"

#include <SDL.h>

#include <cstring>
#include <string>

namespace {

const int W = 480, H = 320;
const ImU32 kPopup = IM_COL32(255, 0, 255, 255);

struct Rig {
    SDL_Surface* surf = nullptr;
    SDL_Renderer* ren = nullptr;
    ImGuiContext* ctx = nullptr;
    okayuitest::Sim sim{(float)W, (float)H};
    OkayUI::CapturedGeometry geo;
    scripteditor::ScriptEditorCodeEdit se;
    bool popup = true, hosted = true, hostTip = false;
    OkayUI::MouseCursor cursor = OkayUI::MouseCursor_Arrow;
    ImVec2 codeMin, codeMax, popMin, popMax;
    Rig() {
        surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
        ren = SDL_CreateSoftwareRenderer(surf);
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2((float)W, (float)H);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::StyleColorsDark();
        ImGui_ImplSDLRenderer2_Init(ren);
        okayuitest::Reset();
        OkayUI::UseLauncherLook(true);
        se.SetText("class Mover {\n    var speed = 4.5;\n    func update(dt) { print(\"idle\"); }\n}");
        se.SetErrorMarkers({{3, "unknown name 'x'"}});
    }
    ~Rig() {
        OkayUI::UseLauncherLook(false);
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui::DestroyContext(ctx);
        SDL_DestroyRenderer(ren);
        SDL_FreeSurface(surf);
    }
    void Frame() {
        ImGui_ImplSDLRenderer2_NewFrame();
        sim.Frame([&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(10, 10));
            ImGui::SetNextWindowSize(ImVec2(460, 300));
            ImGui::Begin("Script", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            const ImVec2 a = ImGui::GetContentRegionAvail(), p = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##code", a);
            codeMin = p; codeMax = ImVec2(p.x + a.x, p.y + a.y);
            okayimgui::StyleLikeImGuiChild(se.Edit(), ImGui::GetStyle());
            if (hosted) {
                okayimgui::DrawOkayUIHere("##code", p, a, [&] { cursor = okayimgui::CursorOf([&] { se.Edit().Draw("##codeedit", a.x, a.y); }); },
                                          okayimgui::SdlRendererTextures(ren), geo);
                okayimgui::SetImGuiCursor(cursor);
            } else {   // the old way: drawn by OkayUI's own frame, after ImGui
                OkayUI::BareNextWindow();
                if (OkayUI::Begin("##code", p.x, p.y, a.x, a.y)) se.Edit().Draw("##codeedit", a.x, a.y);
                OkayUI::End();
            }
            if (hostTip) okayimgui::ShowCodeHover(se.Edit().Hover());   // as ScriptEditorWidget::Render does, right after the code
            if (popup) {   // the autocomplete list: an ImGui popup over the code, opened after the code was laid out
                ImGui::OpenPopup("autocomplete");
                ImGui::SetNextWindowPos(ImVec2(120, 90));
                if (ImGui::BeginPopup("autocomplete")) {
                    ImGui::Dummy(ImVec2(120, 60));
                    popMin = ImGui::GetWindowPos();
                    popMax = ImVec2(popMin.x + ImGui::GetWindowSize().x, popMin.y + ImGui::GetWindowSize().y);
                    ImGui::GetWindowDrawList()->AddRectFilled(popMin, popMax, kPopup);
                    ImGui::EndPopup();
                }
            }
            ImGui::End();
            ImGui::Render();
        });
    }
    // ImGui's picture, then (as the editor does at the end of a frame) whatever is left in OkayUI's frame on top.
    void Present() {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
        OkayUI::EndFrame(ren);
        SDL_RenderFlush(ren);
    }
    unsigned Pixel(int x, int y) {
        SDL_LockSurface(surf);
        const unsigned char* q = (const unsigned char*)surf->pixels + (size_t)y * surf->pitch + (size_t)x * 4;
        const unsigned v = ((unsigned)q[0] << 24) | ((unsigned)q[1] << 16) | ((unsigned)q[2] << 8) | q[3];
        SDL_UnlockSurface(surf);
        return v;
    }
    // Which draw list (in ImGui's draw order) holds the most vertices inside `r`, and which one holds the popup's colour.
    int ListWithCode() {
        const ImDrawData* dd = ImGui::GetDrawData();
        int best = -1, bestN = 0;
        for (int i = 0; i < dd->CmdListsCount; ++i) {
            int n = 0;
            for (const ImDrawVert& v : dd->CmdLists[i]->VtxBuffer)
                if (v.pos.x > codeMin.x + 20 && v.pos.x < codeMax.x - 20 && v.pos.y > codeMin.y + 20 && v.pos.y < codeMax.y - 20) ++n;
            if (n > bestN) { bestN = n; best = i; }
        }
        return best;
    }
    int ListWithPopup() {
        const ImDrawData* dd = ImGui::GetDrawData();
        for (int i = 0; i < dd->CmdListsCount; ++i)
            for (const ImDrawVert& v : dd->CmdLists[i]->VtxBuffer) if (v.col == kPopup) return i;
        return -1;
    }
};

void DrawOrderTests() {
    Rig r;
    for (int i = 0; i < 3; ++i) r.Frame();
    const int codeList = r.ListWithCode(), popList = r.ListWithPopup();
    CHECK(codeList >= 0 && popList >= 0);
    CHECK(popList > codeList);                                   // the popup's rectangle is drawn after the code
    CHECK(r.geo.indices.size() > 300);                           // the code went through the capture (text, washes, numbers)
    if (r.geo.indices.size() <= 300) std::cerr << "  captured indices: " << r.geo.indices.size() << "\n";
    CHECK(OkayUI::GetDrawData().indexCount < 60);                // and is no longer in OkayUI's own frame
    r.Present();
    const int px = (int)((r.popMin.x + r.popMax.x) * 0.5f), py = (int)((r.popMin.y + r.popMax.y) * 0.5f);
    CHECK(r.Pixel(px, py) == 0xff00ffffu);                       // the popup is on top of the code
    // the code is drawn around the popup: its background, not the cleared black
    const unsigned codeBg = r.Pixel((int)r.codeMax.x - 30, (int)r.codeMax.y - 30);
    CHECK(codeBg != 0x000000ffu);
    // the atlas went through the SDL_Renderer texture lookup: text pixels exist in the code rectangle (not just flat fills)
    int distinct = 0; unsigned last = 0;
    for (int x = (int)r.codeMin.x + 60; x < (int)r.codeMin.x + 200; ++x) { const unsigned c = r.Pixel(x, (int)r.codeMin.y + 14); if (c != last) ++distinct; last = c; }
    CHECK(distinct > 6);
    // clicks still reach the editor (input is OkayUI's; only the drawing moved)
    r.popup = false;
    r.sim.Move(r.codeMin.x + 150, r.codeMin.y + 40); r.Frame();
    r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(r.se.Edit().Focused());

    // The old way, for contrast: OkayUI's own frame drawn after ImGui covers the popup.
    r.hosted = false; r.popup = true;
    for (int i = 0; i < 2; ++i) r.Frame();
    r.Present();
    CHECK(r.Pixel(px, py) != 0xff00ffffu);
}

void AppendTests() {
    // AppendGeometry on its own: commands whose texture cannot be given are skipped, flat geometry samples ImGui's white pixel,
    // clip rectangles are pushed, and a large mesh is split under 16-bit indices.
    Rig r;   // ImGui with the SDL_Renderer backend (16-bit indices, vertex offsets allowed as in the editor)
    CHECK((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset) != 0);
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("w");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    OkayUI::CapturedGeometry g;
    auto quad = [&](float x, float y, unsigned char r) {
        const int b = (int)g.vertices.size();
        g.vertices.push_back({x, y, r, 0, 0, 255, 0, 0});
        g.vertices.push_back({x + 2, y, r, 0, 0, 255, 1, 0});
        g.vertices.push_back({x + 2, y + 2, r, 0, 0, 255, 1, 1});
        g.vertices.push_back({x, y + 2, r, 0, 0, 255, 0, 1});
        for (int k : {0, 1, 2, 0, 2, 3}) g.indices.push_back(b + k);
    };
    for (int i = 0; i < 20000; ++i) quad((float)(i % 400), (float)(i / 400), 200);   // 80,000 vertices
    OkayUI::DrawCmd flat; flat.idxOffset = 0; flat.idxCount = (int)g.indices.size() - 6; flat.clipOn = true; flat.clipX = 1; flat.clipY = 2; flat.clipW = 300; flat.clipH = 200;
    OkayUI::DrawCmd tex; tex.idxOffset = (int)g.indices.size() - 6; tex.idxCount = 6; tex.texture = (const void*)&g;   // nobody knows it
    g.cmds = {flat, tex};
    const int vtx0 = dl->VtxBuffer.Size, idx0 = dl->IdxBuffer.Size;
    okayimgui::AppendGeometry(dl, g, [](const void*) { return ImTextureID_Invalid; });
    CHECK(dl->IdxBuffer.Size - idx0 == flat.idxCount);           // the unknown texture's quad is skipped
    CHECK(dl->VtxBuffer.Size - vtx0 >= 80000 - 4);
    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    CHECK(dl->VtxBuffer[vtx0].uv.x == white.x && dl->VtxBuffer[vtx0].uv.y == white.y);
    CHECK(dl->VtxBuffer[vtx0].col == IM_COL32(200, 0, 0, 255));
    bool clipped = false;
    for (const ImDrawCmd& c : dl->CmdBuffer) if (c.ElemCount > 0 && c.ClipRect.z <= 301.0f && c.ClipRect.w <= 202.0f) clipped = true;   // within the asked rectangle
    CHECK(clipped);
    bool inRange = true;   // with 16-bit indices every index stays under 65536 relative to its command's vertex offset
    for (const ImDrawCmd& c : dl->CmdBuffer)
        for (unsigned k = c.IdxOffset; k < c.IdxOffset + c.ElemCount; ++k)
            if ((unsigned)dl->IdxBuffer[(int)k] + c.VtxOffset >= (unsigned)dl->VtxBuffer.Size) inRange = false;
    CHECK(inRange);
    {   // With an offset (multi-window panels: OkayUI's main-window pixels to ImGui's desktop pixels) every position and clip
        // rectangle moves by it; without one nothing moves (the 0,0 default).
        OkayUI::CapturedGeometry one;
        one.vertices = {{10, 20, 9, 9, 9, 255, 0, 0}, {14, 20, 9, 9, 9, 255, 0, 0}, {14, 24, 9, 9, 9, 255, 0, 0}};
        one.indices = {0, 1, 2};
        OkayUI::DrawCmd c; c.idxOffset = 0; c.idxCount = 3; c.clipOn = true; c.clipX = 5; c.clipY = 6; c.clipW = 50; c.clipH = 40;
        one.cmds = {c};
        const int v1 = dl->VtxBuffer.Size;
        okayimgui::AppendGeometry(dl, one, [](const void*) { return ImTextureID_Invalid; }, ImVec2(160.0f, 100.0f));
        CHECK(dl->VtxBuffer.Size - v1 == 3);
        CHECK(dl->VtxBuffer[v1].pos.x == 170.0f && dl->VtxBuffer[v1].pos.y == 120.0f);
        CHECK(dl->VtxBuffer[v1 + 2].pos.x == 174.0f && dl->VtxBuffer[v1 + 2].pos.y == 124.0f);
        bool moved = false;   // the clip rectangle moved too (intersected with the window's own, which starts beyond 165,106)
        for (const ImDrawCmd& dc : dl->CmdBuffer) if (dc.ElemCount == 3 && dc.ClipRect.x >= 165.0f && dc.ClipRect.y >= 106.0f && dc.ClipRect.z <= 215.0f && dc.ClipRect.w <= 146.0f) moved = true;
        CHECK(moved);
        const int v2 = dl->VtxBuffer.Size;
        okayimgui::AppendGeometry(dl, one, [](const void*) { return ImTextureID_Invalid; });
        CHECK(dl->VtxBuffer[v2].pos.x == 10.0f && dl->VtxBuffer[v2].pos.y == 20.0f);
    }
    ImGui::End();
    ImGui::EndFrame();
}


// Which draw list (in ImGui's draw order) belongs to the ImGui window whose name starts with `prefix`, or -1.
int ListOfWindow(const char* prefix) {
    const ImDrawData* dd = ImGui::GetDrawData();
    for (ImGuiWindow* w : GImGui->Windows)
        if (w->Active && std::strncmp(w->Name, prefix, std::strlen(prefix)) == 0)
            for (int i = 0; i < dd->CmdListsCount; ++i) if (dd->CmdLists[i] == w->DrawList) return i;
    return -1;
}

void TooltipLayerTests() {
    // Our hover help drawn as an ImGui tooltip (CodeEdit::SetHostDrawsTooltip + okayimgui::ShowCodeHover): it is an ImGui window in
    // ImGui's own order, drawn after the code and after the popup that was open, and OkayUI's frame draws no tooltip of its own.
    Rig r;
    r.se.Edit().SetHostDrawsTooltip(true);
    r.hostTip = true;
    r.popup = false;
    for (int i = 0; i < 2; ++i) r.Frame();
    const OkayUI::Vec2 at2 = r.se.Edit().PosToScreen(OkayUI::CodePos{2, 0});   // the error marker's line (1-based 3)
    r.sim.Move(at2.x + 3.0f, at2.y + r.se.Edit().LineHeight() * 0.5f);
    r.Frame(); r.Frame();
    CHECK(r.se.Edit().Hover().kind == OkayUI::CodeHover::Error && r.se.Edit().Hover().line == 2);
    const int tip = ListOfWindow("##Tooltip"), code = r.ListWithCode();
    CHECK(tip >= 0 && code >= 0 && tip > code);                  // an ImGui tooltip, drawn after the code
    CHECK(OkayUI::GetDrawData().indexCount < 60);                // and nothing of it in OkayUI's frame
    // with the autocomplete popup open over the code, the tooltip still comes after it (ImGui stacks its tooltips on top)
    r.popup = true;
    r.Frame(); r.Frame();
    const int tip2 = ListOfWindow("##Tooltip"), pop = r.ListWithPopup();
    CHECK(tip2 >= 0 && pop >= 0 && tip2 > pop);
    if (!(tip2 > pop)) std::cerr << "  tooltip list " << tip2 << ", popup list " << pop << "\n";
    // OkayUI's own tooltip, for contrast: drawn in OkayUI's frame, outside ImGui's order
    r.se.Edit().SetHostDrawsTooltip(false);
    r.hostTip = false; r.popup = false;
    r.sim.Move(at2.x + 3.0f, at2.y + r.se.Edit().LineHeight() * 0.5f);
    r.Frame(); r.Frame();
    CHECK(ListOfWindow("##Tooltip") < 0);
    CHECK(OkayUI::GetDrawData().indexCount > 60);
}

void CursorTests() {
    // the I-beam over the code's text reaches ImGui (whose backend sets the system cursor), as the ImGui editor shows it; the
    // arrow over the gutter; OkayUI's own cursor of the frame is left as it was
    Rig r;
    r.popup = false;
    for (int i = 0; i < 2; ++i) r.Frame();
    const OkayUI::Vec2 t = r.se.Edit().PosToScreen(OkayUI::CodePos{1, 6});
    r.sim.Move(t.x + 2.0f, t.y + 4.0f);
    r.Frame();
    CHECK(r.cursor == OkayUI::MouseCursor_TextInput && ImGui::GetMouseCursor() == ImGuiMouseCursor_TextInput);
    r.sim.Move(r.codeMin.x + 12.0f, t.y + 4.0f);                    // the gutter
    r.Frame();
    CHECK(r.cursor == OkayUI::MouseCursor_Arrow && ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow);
    OkayUI::Input in; in.mouseX = 1; in.mouseY = 1;
    OkayUI::BeginFrame(in);
    OkayUI::SetMouseCursor(OkayUI::MouseCursor_Hand);
    const OkayUI::MouseCursor got = okayimgui::CursorOf([] {});
    CHECK(got == OkayUI::MouseCursor_Arrow && OkayUI::GetMouseCursor() == OkayUI::MouseCursor_Hand);
    OkayUI::EndFrameData();
}

void KeyboardClaimTests() {
    // okayimgui::ClaimKeyboardForCode: while our editor has the keyboard, ImGui's keyboard navigation must not move the nav cursor
    // off the code (arrows) or press a button (Space, Enter); the script editor's own shortcuts (Ctrl+S) still see their keys;
    // io.WantTextInput / WantCaptureKeyboard are set; the typed characters are taken out of ImGui's queue.
    Rig r;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGuiID codeId = 0, otherId = 0;
    int otherPressed = 0;
    bool claim = true, sawCtrlS = false, wantText = false, queueLeft = false;
    auto frame = [&] {
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(300, 250));
        ImGui::Begin("Keys", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGui::InvisibleButton("##code", ImVec2(200, 100));
        codeId = ImGui::GetItemID();
        if (ImGui::Button("Other", ImVec2(120, 30))) ++otherPressed;
        otherId = ImGui::GetItemID();
        sawCtrlS = io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false);   // as the script editor's Ctrl+S
        if (claim) okayimgui::ClaimKeyboardForCode(codeId);
        wantText = io.WantTextInput && io.WantCaptureKeyboard;
        queueLeft = io.InputQueueCharacters.Size > 0;
        ImGui::End();
        ImGui::Render();
    };
    auto press = [&](ImGuiKey k) { io.AddKeyEvent(k, true); frame(); io.AddKeyEvent(k, false); frame(); };
    io.AddMousePosEvent(60, 60); frame();
    io.AddMouseButtonEvent(0, true); frame(); io.AddMouseButtonEvent(0, false); frame();
    io.AddMousePosEvent(400, 300); frame();
    CHECK(GImGui->NavId == codeId);                              // the click put ImGui's nav cursor on the code
    CHECK(wantText);
    for (int i = 0; i < 4; ++i) press(ImGuiKey_DownArrow);
    CHECK(GImGui->NavId == codeId);                              // claimed: the arrows did not move it
    press(ImGuiKey_Space); press(ImGuiKey_Enter); press(ImGuiKey_Tab); press(ImGuiKey_Escape);
    CHECK(otherPressed == 0 && GImGui->NavId == codeId && GImGui->NavWindow && std::strcmp(GImGui->NavWindow->Name, "Keys") == 0);
    io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_S, true); frame();
    CHECK(sawCtrlS);                                             // the script editor's Ctrl+S still sees S
    io.AddKeyEvent(ImGuiKey_S, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); frame();
    io.AddInputCharacter('q'); frame();
    CHECK(!queueLeft);                                           // our editor typed it; nothing later in the frame sees it
    // Not claimed (the old state of our editor): the arrows walk the nav cursor to the button and Space presses it.
    claim = false;
    frame(); frame();
    for (int i = 0; i < 4 && GImGui->NavId != otherId; ++i) press(ImGuiKey_DownArrow);
    CHECK(GImGui->NavId == otherId);
    press(ImGuiKey_Space);
    CHECK(otherPressed == 1);
    CHECK(!wantText);
}
} // namespace

int main() {
    if (SDL_Init(0) != 0) { std::cerr << "SDL_Init failed\n"; return 1; }
    std::cout << "[suite] code editor hosted in ImGui\n";
    DrawOrderTests();
    AppendTests();
    TooltipLayerTests();
    CursorTests();
    KeyboardClaimTests();
    std::cout << okaytest::g_checks << " checks, " << okaytest::g_failures << " failures\n";
    SDL_Quit();
    return okaytest::g_failures == 0 ? 0 : 1;
}
