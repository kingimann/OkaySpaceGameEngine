// The shapes a Dear ImGui host reads and writes (docs/own_ui_toolkit.md 6.22), headless: the ImDrawData view of OkayUI's draw data (what a renderer
// written for Dear ImGui consumes), io.InputQueueCharacters and AddKeyEvent / AddInputCharacter, io.Fonts requests, the viewport pointer with its host
// fields, the typed platform callbacks, and the small pointer-like forms (StateStorage, the modal popup entry, BeginMenuBar as bool).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIInternals.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {
int g_created = 0, g_rendered = 0;
void CreateWin(UI::Viewport*) { ++g_created; }
void RenderWin(UI::Viewport*, void*) { ++g_rendered; }
}

int main() {
    RUN_SUITE("okayui host shapes");

    {   // ---- ImDrawData view ----
        Reset(); Sim s(320, 240);
        s.Frame([&] {
            UI::SetNextWindowPos(UI::Vec2{10, 10}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{200, 120}, UI::Cond_Always);
            UI::Begin("w", nullptr, 0);
            UI::Text("hello"); UI::Button("go");
            UI::End();
            UI::Render();
        });
        const UI::DrawData raw = UI::GetDrawData();
        UI::ImDrawData* dd = UI::GetDrawData();
        CHECK(raw.vertexCount > 100 && raw.indexCount > 100 && raw.cmdCount >= 1);
        CHECK(dd && dd->Valid && dd->CmdListsCount == 1 && dd->CmdLists.Size == 1 && dd->TotalVtxCount == raw.vertexCount && dd->TotalIdxCount == raw.indexCount);
        const UI::ImDrawData* const d2 = dd;
        const UI::DrawList* dl = d2->CmdLists[0];
        CHECK(dl->VtxBuffer.Size == raw.vertexCount && dl->IdxBuffer.Size == raw.indexCount && dl->CmdBuffer.Size >= 1);
        bool same = true;
        const UI::DrawVertex* rv = static_cast<const UI::DrawVertex*>(raw.vertices);
        for (int i = 0; i < raw.vertexCount && same; ++i) {
            const UI::ImDrawVert& v = dl->VtxBuffer[i];
            const unsigned col = (unsigned)rv[i].r | ((unsigned)rv[i].g << 8) | ((unsigned)rv[i].b << 16) | ((unsigned)rv[i].a << 24);
            same = v.pos.x == rv[i].x && v.pos.y == rv[i].y && v.uv.x == rv[i].u && v.uv.y == rv[i].v && v.col == col;
        }
        CHECK(same);
        long long elems = 0; bool idxOk = true;
        for (const UI::ImDrawCmd& c : dl->CmdBuffer) { elems += c.ElemCount; idxOk = idxOk && c.IdxOffset + c.ElemCount <= (unsigned)dl->IdxBuffer.Size && c.ElemCount % 3 == 0 && c.VtxOffset == 0 && c.UserCallback == nullptr; }
        CHECK(idxOk && elems == raw.indexCount);
        bool inRange = true; for (int i = 0; i < dl->IdxBuffer.Size; ++i) inRange = inRange && dl->IdxBuffer[i] < (unsigned)dl->VtxBuffer.Size;
        CHECK(inRange);
        CHECK(d2->DisplaySize.x == 320.0f && d2->DisplaySize.y == 240.0f && d2->FramebufferScale.x == 1.0f && d2->Textures && d2->Textures->Size == 0);   // classic look: no atlas
        // a clipped command carries its rectangle; an unclipped one the "no clip" box
        bool hasClip = false, hasOpen = false;
        for (const UI::ImDrawCmd& c : dl->CmdBuffer) { if (c.ClipRect.z < 1000.0f) hasClip = true; else hasOpen = true; }
        CHECK(hasClip || hasOpen);
        // converting again rebuilds in place (same pointer, same counts); an empty frame gives an empty but valid list
        CHECK(static_cast<UI::ImDrawData*>(UI::GetDrawData()) == dd && dd->TotalVtxCount == raw.vertexCount);
        s.Frame([&] {}); UI::ImDrawData* e = UI::GetDrawData();
        CHECK(e->Valid && e->TotalVtxCount == 0 && e->TotalIdxCount == 0 && e->CmdLists[0]->CmdBuffer.Size == 0);
    }

    {   // ---- typed characters ----
        Reset(); Sim s(320, 240);
        s.text = "a\xC3\xA9\xE2\x82\xAC";   // a, e-acute, euro
        s.Frame([&] {
            UI::IO& io = UI::GetIO();
            CHECK(io.InputQueueCharacters.Size == 3 && io.InputQueueCharacters[0] == 'a' && io.InputQueueCharacters[1] == 0xE9 && io.InputQueueCharacters[2] == 0x20AC);
        });
        char buf[32] = ""; UI::Rect R;
        auto field = [&] { UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{300, 100}, UI::Cond_Always); UI::Begin("t", nullptr, 0); UI::InputText("##in", buf, sizeof buf); R = UI::GetItemRect(); UI::End(); };
        s.Frame(field);
        const UI::Rect r = R;
        s.Move(r.x + 20, r.y + r.h * 0.5f); s.Down(); s.Frame(field); s.Up(); s.Frame(field);
        s.text = "xy"; s.Frame(field);
        CHECK(std::string(buf) == "xy");
        s.text = "zz";   // the ported code takes the typing: the field must not see it
        s.Frame([&] { UI::GetIO().InputQueueCharacters.resize(0); field(); });
        CHECK(std::string(buf) == "xy");
        s.text = "q"; s.Frame(field);
        CHECK(std::string(buf) == "xyq");
        // AddInputCharacter / AddKeyEvent through NewFrame
        UI::GetIO().AddInputCharacter('w'); UI::GetIO().AddInputCharacter(0xE9); UI::GetIO().AddInputCharacter(0xD800);   // the last is not a character
        UI::NewFrame(); field(); UI::EndFrameData();
        CHECK(std::string(buf) == "xyqw\xC3\xA9");
        bool backspaceSeen = false;
        UI::GetIO().AddKeyEvent(UI::KeyId_Backspace, true); UI::GetIO().AddKeyEvent(UI::KeyId_Backspace, false);
        UI::NewFrame(); field(); backspaceSeen = true; UI::EndFrameData();
        CHECK(backspaceSeen && std::string(buf) == "xyqw");
        UI::GetIO().AddKeyEvent(-1, true); UI::GetIO().AddKeyEvent(100000, true); UI::GetIO().AddInputCharacter(0);   // bad arguments change nothing
        UI::NewFrame(); field(); UI::EndFrameData();
        CHECK(std::string(buf) == "xyqw");
    }

    {   // ---- io.Fonts ----
        Reset();
        UI::IO& io = UI::GetIO();
        CHECK(io.Fonts != nullptr);
        UI::ImFontConfig fc; fc.SizePixels = 18.0f; fc.MergeMode = true;
        UI::ImFont* a = io.Fonts->AddFontFromMemoryCompressedBase85TTF("abc", 18.0f, &fc);
        UI::ImFont* b = io.Fonts->AddFontDefault();
        UI::ImFont* c = io.Fonts->AddFontFromFileTTF("x.ttf", 22.0f);
        CHECK(a && b && c && a != b && b != c && a != c);
        int n = 0; const UI::FontRequest* rq = UI::FontRequests(&n);
        CHECK(n >= 3 && rq[n - 3].kind == UI::FontRequest::FromCompressedBase85 && rq[n - 3].px == 18.0f && rq[n - 3].merge && rq[n - 3].font == a
              && rq[n - 2].kind == UI::FontRequest::Default && rq[n - 1].kind == UI::FontRequest::FromFile && std::string(rq[n - 1].file) == "x.ttf" && rq[n - 1].px == 22.0f);
        const UI::Font* before = UI::GetFont();
        UI::PushFont(a); UI::PopFont();
        CHECK(UI::GetFont() == before);
        CHECK(io.Fonts->Build());
        for (int i = 0; i < 60; ++i) io.Fonts->AddFontDefault();   // past the list size: still answered
        CHECK(io.Fonts->AddFontDefault() != nullptr);
    }

    {   // ---- viewport pointer and platform callbacks ----
        Reset(); Sim s(640, 480);
        s.Frame([&] {});
        UI::Viewport* vp = UI::GetMainViewport();
        CHECK(vp && vp->ID != 0 && vp->Size.x == 640.0f && vp->Size.y == 480.0f && vp->WorkSize.y == 480.0f);
        int token = 5; vp->RendererUserData = &token; vp->PlatformHandle = &token;
        UI::Viewport* again = UI::GetMainViewport();
        CHECK(again == vp && again->RendererUserData == &token && again->ID == vp->ID);   // one persistent object
        vp->DrawData = UI::GetDrawData();
        UI::Viewport* vp2 = UI::GetMainViewport();
        CHECK(vp2->DrawData != nullptr && vp2->DrawData == vp->DrawData);
        const UI::Viewport copy = UI::GetMainViewport();
        CHECK(copy.GetCenter().x == 320.0f);
        UI::PlatformIO& pio = UI::GetPlatformIO();
        pio.Renderer_CreateWindow = CreateWin; pio.Renderer_RenderWindow = RenderWin;
        pio.Renderer_CreateWindow(vp); pio.Renderer_RenderWindow(vp, nullptr);
        CHECK(g_created == 1 && g_rendered == 1);
        vp->RendererUserData = nullptr; pio.Renderer_CreateWindow = nullptr; pio.Renderer_RenderWindow = nullptr;
    }

    {   // ---- pointer-like forms ----
        Reset(); Sim s(320, 240);
        UI::StateStorage st;
        st.SetInt(7u, 42);
        UI::StateStorage* p = st;   // conversion
        CHECK(p && p->GetInt(7u) == 42 && st->GetInt(7u) == 42);
        UI::GetStateStorage()->SetInt(9u, 3);
        CHECK(UI::GetStateStorage()->GetInt(9u) == 3);
        UI::StateStorageReset();
        bool menuBegan = true, modalAtStart = true, modalOpen = false; UI::WindowRef* modalRef = nullptr;
        s.Frame([&] {
            modalAtStart = UI::GetTopMostPopupModal() != nullptr;
            UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{300, 200}, UI::Cond_Always);
            UI::Begin("m", nullptr, 0);
            menuBegan = UI::BeginMenuBar(); UI::EndMenuBar();
            UI::SetWindowSize(UI::Vec2{250, 150});
            if (UI::BeginChild(UI::GetID("kid"), UI::Vec2{100, 40}, true)) { UI::Text("x"); UI::EndChild(); }
            UI::PushClipRect(UI::Vec2{0, 0}, UI::Vec2{100, 100}, true); UI::PopClipRect();
            UI::OpenPopup("modal");
            UI::End();
        });
        const UI::WindowRef* win = UI::FindWindowRefByName("m");
        CHECK(win && win->Size.x == 250.0f && win->Size.y == 150.0f);   // SetWindowSize(Vec2) of the current window
        s.Frame([&] {
            UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{300, 200}, UI::Cond_Always);
            UI::Begin("m", nullptr, 0);
            if (UI::BeginPopupModal("modal", nullptr, 0)) { modalRef = UI::GetTopMostPopupModal(); modalOpen = modalRef != nullptr; UI::EndPopup(); }
            UI::End();
        });
        UI::Context* gc = UI::RefreshContext();
        CHECK(gc->OpenPopupStack.Size >= 1 && gc->OpenPopupStack[0].Window && gc->OpenPopupStack[0].Window->Flags == UI::Window_Modal && gc->OpenPopupStack[0].Window->Rect().GetWidth() > 10.0f);
        CHECK(!modalAtStart && menuBegan && modalOpen && modalRef && modalRef->ID != 0 && modalRef->Name[0] == 0);
        // the window item width default: written through DC, applied to the next items of the window
        float wBefore = 0, wAfter = 0;
        s.Frame([&] {
            UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{300, 200}, UI::Cond_Always);
            UI::Begin("dc", nullptr, 0);
            wBefore = UI::CalcItemWidth();
            UI::WindowRef* me = UI::GetCurrentWindow();
            me->DC.ItemWidth = 77.0f;
            wAfter = UI::CalcItemWidth();
            UI::End();
        });
        CHECK(wBefore > 100.0f && std::fabs(wAfter - 77.0f) < 1.0f);
        s.Frame([&] {
            UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{300, 200}, UI::Cond_Always);
            UI::Begin("dc", nullptr, 0);
            CHECK(UI::CalcItemWidth() > 100.0f);   // the default ended with the window
            UI::End();
        });
        // ImRect::ClipWith
        UI::ImRect r(0, 0, 10, 10); r.ClipWith(UI::ImRect(5, 5, 20, 20));
        CHECK(r.Min.x == 5 && r.Max.x == 10 && r.Min.y == 5 && r.Max.y == 10);
        // ImVector
        UI::ImVector<int> v; for (int i = 0; i < 20; ++i) v.push_back(i);
        v.push_front(-1); v.erase(v.Data + 5);
        CHECK(v.Size == 20 && v[0] == -1 && v[1] == 0 && v[5] == 5 && v.find_index(19) == 19 && v.find_index(99) == -1);
        UI::ImVector<int> w = v; w.clear(); CHECK(v.Size == 20 && w.empty());
        UI::ImVector<int*> e; e.erase(e.Data); CHECK(e.Size == 0);
    }
    TEST_MAIN_RESULT();
}
