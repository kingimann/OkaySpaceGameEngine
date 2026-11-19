// Step 6.13: the Dear ImGui spellings (okay/UI/OkayUIImGuiNames.hpp, generated) compile and act as the OkayUI calls they stand for, and the
// argument shapes ported code uses (pointer-like draw list, viewport arrow, payload fields, pivot, popup / slider / combo flags) work.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIImGuiNames.hpp"

#include <cmath>
#include <cstring>
#include <string>

using namespace okayuitest;

int main() {
    RUN_SUITE("okayui ImGui spellings");

    {   // a Dear ImGui style fragment, written as ImGui:: code, compiles and runs
        Reset(); Sim s(300, 200);
        int clicks = 0; float f = 0.5f; int sel = 1; ImVec2 vp(0, 0); ImRect r; bool combo = false;
        auto ui = [&] {
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always); ImGui::SetNextWindowSize(ImVec2(260, 160), ImGuiCond_Always);
            if (ImGui::Begin("Probe", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("Go", ImVec2(80, 0))) ++clicks;
                r = ImGui::GetItemImRect();
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 12.0f);
                ImGui::Text("%d clicks, %s", clicks, "ok");
                ImGui::SliderFloat("##f", &f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                combo = ImGui::Combo("##c", &sel, "One\0Two\0Three\0");
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(ImVec2(30, 150), ImVec2(60, 170), IM_COL32(200, 100, 50, 255), 4.0f, ImDrawFlags_RoundCornersTop);
                dl->AddText(ImGui::GetFont(), 16.0f, ImVec2(70, 150), IM_COL32(255, 255, 255, 255), "hi");
                const ImGuiStyle& st = ImGui::GetStyle(); (void)st.ItemSpacing.x;
                vp = ImGui::GetMainViewport()->GetCenter();
            }
            ImGui::End();
        };
        s.Frames(2, ui);
        s.Click(r.Min.x + 5, r.Min.y + 5, ui);
        CHECK(clicks == 1 && vp.x == 150 && vp.y == 100);
        CHECK(ImGui::GetStyleColorVec4(ImGuiCol_Text).w > 0.0f);
        (void)combo;
    }
    {   // pointer-like draw list: -> and conversion to ImDrawList*; the reference form still works
        Reset(); Sim s(100, 100);
        s.Frame([&] {
           
            OkayUI::BareNextWindow();
            if (OkayUI::Begin("w", 0, 0, 100, 100)) {
                ImDrawList* a = OkayUI::GetWindowDrawList();
                const OkayUI::DrawList& b = OkayUI::GetWindowDrawList();
                CHECK(a == &b && !a->overlay && OkayUI::GetForegroundDrawList()->overlay);
                OkayUI::GetWindowDrawList()->AddRectFilled(ImVec2(10, 10), ImVec2(40, 40), 0xff0000ffu);
                OkayUI::GetWindowDrawList().AddRectFilled(OkayUI::Vec2{50, 50}, OkayUI::Vec2{80, 80}, 0xff0000ffu);
            }
            OkayUI::End();
        });
        const Bitmap b = s.Draw(0, 0, 0);
        CHECK(PixelAt(b, 25, 25) == Rgba(255, 0, 0) && PixelAt(b, 65, 65) == Rgba(255, 0, 0));
    }
    {   // corner flags: the bottom corners of a top-rounded rectangle are square, the top ones are cut
        Reset(); Sim s(100, 100);
        s.Frame([&] {
            OkayUI::BareNextWindow();
            if (OkayUI::Begin("w", 0, 0, 100, 100)) { ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(10, 10), ImVec2(90, 50), 0xffffffffu, 12.0f, ImDrawFlags_RoundCornersTop); }
            OkayUI::End();
        });
        const Bitmap b = s.Draw(0, 0, 0);
        CHECK(PixelAt(b, 10, 10) == Rgba(0, 0, 0) && PixelAt(b, 11, 11) == Rgba(0, 0, 0));       // cut top-left corner
        CHECK(PixelAt(b, 11, 49) == Rgba(255, 255, 255) && PixelAt(b, 88, 49) == Rgba(255, 255, 255));   // square bottom corners
        CHECK(PixelAt(b, 50, 30) == Rgba(255, 255, 255));
    }
    {   // payload fields, pivot, Dummy(vec), Selectable(label), keys, ImColor, font size
        Reset(); Sim s(320, 240);
        ImVec2 pos(0, 0);
        s.Frames(2, [&] {
            ImGui::SetNextWindowPos(ImVec2(160, 120), ImGuiCond_Always, ImVec2(0.5f, 0.5f)); ImGui::SetNextWindowSize(ImVec2(100, 60), ImGuiCond_Always);
            if (ImGui::Begin("Pivot")) { pos = ImGui::GetWindowPos(); ImGui::Dummy(ImVec2(10, 10)); ImGui::Selectable("row"); }
            ImGui::End();
        });
        CHECK(pos.x == 110 && pos.y == 90);
        static_assert(ImGuiKey_UpArrow == OkayUI::KeyId_Up && ImGuiKey_LeftShift == OkayUI::KeyId_Shift && ImGuiKey_Keypad0 > OkayUI::KeyId_Alt, "keys");
        ImColor c(255, 128, 0); OkayUI::Vec4 v = c; ImU32 u = c;
        CHECK(std::fabs(v.x - 1.0f) < 1e-6f && std::fabs(v.y - 128 / 255.0f) < 1e-6f && u == IM_COL32(255, 128, 0, 255));
        const ImFont* font = ImGui::GetFont(); const ImVec2 ts = font->CalcTextSizeA(16.0f, 1e9f, 0.0f, "abc\nde");
        CHECK(ts.y == 32.0f && ts.x == 3 * 16.0f);
        ImGuiPayload pl; std::strcpy(pl.type, "ASSET"); CHECK(pl.IsDataType("ASSET") && !pl.IsDataType("OTHER"));
        CHECK(ImGui::FindRenderedTextEnd("a##b") != nullptr);
        CHECK(ImGui::CalcTextSize("ab##cd", nullptr, true).x == ImGui::CalcTextSize("ab").x);
    }
    {   // payload fields come with the copies the drag and drop calls hand out
        Reset(); Sim s(300, 100);
        OkayUI::Rect src, dst; const OkayUI::DragPayload* got = nullptr; bool delivered = false;
        auto ui = [&] {
            OkayUI::BareNextWindow();
            if (OkayUI::Begin("w", 0, 0, 300, 100)) {
                OkayUI::Button("src"); src = OkayUI::GetItemRect();
                if (OkayUI::BeginDragDropSource()) { const char text[] = "hello"; OkayUI::SetDragDropPayload("ASSET_PATH", text, sizeof text); OkayUI::EndDragDropSource(); }
                OkayUI::SameLine(100, -1); OkayUI::Button("dst"); dst = OkayUI::GetItemRect();
                if (OkayUI::BeginDragDropTarget()) {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH", ImGuiDragDropFlags_AcceptBeforeDelivery)) { got = p; if (p->IsDelivery()) delivered = true; }
                    OkayUI::EndDragDropTarget();
                }
            }
            OkayUI::End();
        };
        s.Frame(ui); s.Move(CenterX(src), CenterY(src)); s.Frame(ui); s.Down(); s.Frame(ui);
        s.Move(CenterX(dst), CenterY(dst)); s.Frames(2, ui);
        CHECK(got && got->IsDataType("ASSET_PATH") && got->Data != nullptr && std::string((const char*)got->Data) == "hello" && got->DataSize == 6);
        s.Up(); s.Frame(ui); CHECK(delivered);
    }
    {   // Window_NoDecoration has no title bar; popup flags, InputText_CharsDecimal
        Reset(); Sim s(260, 120);
        auto draw = [&](int flags) {
            s.Frames(2, [&] { OkayUI::SetNextWindowPos({10, 10}); OkayUI::SetNextWindowSize({200, 80}); if (OkayUI::Begin("d", nullptr, flags)) OkayUI::Dummy(5, 5); OkayUI::End(); });
            return s.Draw(0, 0, 0);
        };
        const Bitmap plain = draw(0), bare = draw(ImGuiWindowFlags_NoDecoration);
        CHECK(plain.rgba != bare.rgba);
        Reset(); Sim s2(240, 40); char t[16] = ""; OkayUI::Rect r;
        auto ui = [&] { OkayUI::BareNextWindow(); if (OkayUI::Begin("w", 0, 0, 240, 40)) { OkayUI::InputText("##a", t, sizeof t, ImGuiInputTextFlags_CharsDecimal); r = OkayUI::GetItemRect(); } OkayUI::End(); };
        s2.Frame(ui); s2.Click(CenterX(r), CenterY(r), ui); s2.Type("1a2.b-3 4"); s2.Frame(ui);
        CHECK(std::string(t) == "12.-34");
        Reset(); Sim s3(100, 40); bool leftOpened = true, rightOpened = false;
        s3.Frame([&] { OkayUI::BareNextWindow(); if (OkayUI::Begin("w", 0, 0, 100, 40)) { OkayUI::Button("b"); leftOpened = OkayUI::BeginPopupContextItem("p", OkayUI::Popup_MouseButtonLeft); rightOpened = OkayUI::BeginPopupContextItem("q", ImGuiPopupFlags_MouseButtonRight); if (rightOpened) OkayUI::EndPopup(); } OkayUI::End(); });
        CHECK(!leftOpened && !rightOpened);
    }
    {   // the window / context accessor layer (what the Inspector reads): GImGui, ImGuiContext fields, ImGuiWindow fields
        Reset(); Sim s(400, 260);
        std::string hovered, current, activeWin; unsigned activeId = 0; const ImGuiWindow* parent = reinterpret_cast<const ImGuiWindow*>(1); OkayUI::Rect btn;
        int a = 0, b = 0, clicksA = 0, clicksB = 0; OkayUI::Rect ra, rb;
        auto ui = [&] {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always); ImGui::SetNextWindowSize(ImVec2(180, 190), ImGuiCond_Always);
            if (ImGui::Begin("Alpha##x")) {
                ImGui::Button("go"); btn = ImGui::GetItemRect();
                ImGuiContext& g = *GImGui;
                if (g.CurrentWindow) current = g.CurrentWindow->Name;
                ImGui::PushID(&a); if (ImGui::Button("same")) ++clicksA; ra = ImGui::GetItemRect(); ImGui::PopID();
                ImGui::PushID(&b); if (ImGui::Button("same")) ++clicksB; rb = ImGui::GetItemRect(); ImGui::PopID();
            }
            ImGui::End();
            ImGui::SetNextWindowPos(ImVec2(210, 10), ImGuiCond_Always); ImGui::SetNextWindowSize(ImVec2(180, 120), ImGuiCond_Always);
            if (ImGui::Begin("Beta")) ImGui::Dummy(ImVec2(5, 5));
            ImGui::End();
            ImGuiContext& g = *GImGui;
            hovered = g.HoveredWindow ? g.HoveredWindow->Name : "";
            activeId = g.ActiveId; activeWin = g.ActiveIdWindow ? g.ActiveIdWindow->Name : "";
            parent = g.HoveredWindow ? g.HoveredWindow->ParentWindow : nullptr;
        };
        s.Move(300, 100); s.Frames(2, ui);
        CHECK(hovered == "Beta" && current == "Alpha##x" && activeId == 0 && activeWin.empty() && parent == nullptr);
        s.Move(btn.x + 5, btn.y + 5); s.Frame(ui);
        CHECK(hovered == "Alpha##x");
        s.Down(); s.Frame(ui);
        CHECK(activeId != 0 && activeWin == "Alpha##x");
        s.Up(); s.Frame(ui);
        s.Move(395, 255); s.Frame(ui); CHECK(hovered.empty());
        s.blocked = true; s.Move(btn.x + 5, btn.y + 5); s.Frame(ui); CHECK(hovered.empty()); s.blocked = false;
        s.Click(CenterX(ra), CenterY(ra), ui); CHECK(clicksA == 1 && clicksB == 0);
        s.Click(CenterX(rb), CenterY(rb), ui); CHECK(clicksA == 1 && clicksB == 1);
        ImGuiContext* c = ImGui::CreateContext(); CHECK(c == GImGui && ImGui::GetCurrentContext() == c);
    }
    {   // host-level calls: NewFrame, platform no-ops and PlatformIO, ClosePopupToLevel, undock by window ref, names
        Reset(); Sim s(300, 200);
        s.Frame([] {});
        const int fc = ImGui::GetFrameCount();
        ImGui::NewFrame(); ImGui::EndFrameData();                     // starts a frame with the last input
        CHECK(ImGui::GetFrameCount() == fc + 1);
        ImGui::UpdatePlatformWindows(); ImGui::RenderPlatformWindowsDefault();
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        pio.Renderer_CreateWindow = reinterpret_cast<void*>(1);
        CHECK(pio.Viewports.Size == 1 && ImGui::GetPlatformIO().Renderer_CreateWindow == reinterpret_cast<void*>(1));
        bool open = true;
        auto ui = [&] {
            OkayUI::BareNextWindow();
            if (OkayUI::Begin("w", 0, 0, 300, 200)) { if (open) { OkayUI::OpenPopup("p"); open = false; } if (OkayUI::BeginPopup("p")) { OkayUI::Text("x"); OkayUI::EndPopup(); } }
            OkayUI::End();
        };
        s.Frames(3, ui); CHECK(OkayUI::IsPopupOpen("p"));
        ImGui::ClosePopupToLevel(0, true); s.Frames(2, ui); CHECK(!OkayUI::IsPopupOpen("p"));
        // undock: a docked window becomes a floating one; an unknown or null ref does nothing
        OkayUI::DockLayout& lay = OkayUI::GetDockLayout();
        lay.DockRoot("docked one", OkayUI::DockSide_Left); lay.DockRoot("docked two", OkayUI::DockSide_Right);
        OkayUI::WindowRef dref; std::strcpy(dref.Name, "docked one"); dref.Pos = {20, 30}; dref.Size = {200, 100};
        CHECK(lay.Contains("docked one") && !lay.IsFloating("docked one"));
        CHECK(ImGui::DockContextQueueUndockWindow(nullptr, &dref) && lay.IsFloating("docked one") && !ImGui::DockContextQueueUndockWindow(nullptr, &dref));
        OkayUI::WindowRef ref; std::strcpy(ref.Name, "nowhere"); ref.Size = {100, 80};
        CHECK(!ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(), &ref) && !ImGui::DockContextQueueUndockWindow(nullptr, nullptr));
        static_assert(ImGuiAxis_Y == 1 && ImGuiSortDirection_Ascending == 1 && ImGuiTableBgTarget_CellBg == OkayUI::TableBg_CellBg, "names");
    }
    TEST_MAIN_RESULT();
}
