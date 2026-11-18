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
    TEST_MAIN_RESULT();
}
