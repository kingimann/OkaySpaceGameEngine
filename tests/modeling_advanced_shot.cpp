// Test-only picture helper (NOT part of the product, not in CMake): a stand-in for the Modeling window (the Easy page is the real one) that draws the same
// sidebar (pages under job headings), the tool search box with its result rows, the button outline for matches and the hint line,
// using editor/ModelingToolIndex.hpp. The page body is built from the tool index (one button per tool, grouped by section), NOT the
// real page code, which lives in editor/main.cpp and cannot be run headless. Build like tests/modeling_easy_shot.cpp.
#include "ModelingToolIndex.hpp"
#include "ModelingSidebar.hpp"
#include "ModelingEasyUi.hpp"
#include "ModelingStarters.hpp"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#include "okay/Platform/BmpWriter.hpp"
#include <string>
namespace mt = oked::modeltools;

static void Save(SDL_Renderer* r, int w, int h, const std::string& path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    okay::platform::WriteBmp32(path.c_str(), s->pixels, w, h, s->pitch); SDL_FreeSurface(s);
}
static void Draw(int page, const std::string& query, const std::string& hint, bool folded = false) {
    mt::SidebarState sb; sb.folded = folded;
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(1280, 720));
    ImGui::Begin("Modeling", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::BeginChild("##nav", ImVec2(mt::SidebarWidth(sb, 1280), 0), ImGuiChildFlags_Borders);
    ImGui::Button(folded ? ">##fold" : "<##fold", ImVec2(32, 32));
    const char* lastJob = "";
    for (const mt::Page& pi : mt::Pages()) {
        if (folded) { ImGui::PushID(pi.legacy); ImGui::Selectable(pi.abbr, page == pi.legacy, 0, ImVec2(0, 34)); ImGui::PopID(); continue; }
        if (std::string(pi.job) != lastJob) { lastJob = pi.job; ImGui::Dummy(ImVec2(0, 2)); ImGui::TextDisabled("%s", pi.job); }
        ImGui::PushID(pi.legacy);
        const ImVec2 r0 = ImGui::GetCursorScreenPos();
        ImGui::Selectable("##pg", page == pi.legacy, 0, ImVec2(0, 44));
        ImGui::GetWindowDrawList()->AddText(ImVec2(r0.x + 8, r0.y + 5), ImGui::GetColorU32(ImGuiCol_Text), pi.name);
        ImGui::GetWindowDrawList()->AddText(ImVec2(r0.x + 8, r0.y + 5 + ImGui::GetTextLineHeight()), ImGui::GetColorU32(ImGuiCol_TextDisabled), pi.desc);
        ImGui::PopID();
    }
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10)); ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));
    char buf[40]; std::snprintf(buf, sizeof buf, "%s", query.c_str());
    ImGui::SetNextItemWidth(-90); ImGui::InputTextWithHint("##find", "Find a tool (try: bevel, round, mirror, uv)...", buf, sizeof buf);
    ImGui::SameLine(); ImGui::Button("Clear##find");
    if (!query.empty()) for (const mt::Hit& h : mt::Search(query, 8)) {
        const mt::Page* p = mt::FindPage(h.tool->page);
        ImGui::Selectable((std::string(h.tool->name) + "   -   " + p->name + " > " + h.tool->section + "##" + h.tool->id).c_str());
    }
    ImGui::Separator();
    ImGui::TextUnformatted(mt::FindPage(page)->name); ImGui::SameLine(); ImGui::TextDisabled("  %s", hint.empty() ? mt::FindPage(page)->desc : hint.c_str());
    ImGui::Separator();
    if (page == 8) {   // the Easy page: the real drawing code with a stand-in host
        static okay::Mesh mesh = oked::easymodel::BuildStarter("house");
        oked::easymodel_ui::Host h; h.mesh = &mesh; h.state.haveObject = true; h.state.editMode = true; h.state.selMode = 2; h.state.selCount = 2;
        h.canUndo = [] { return true; }; h.canRedo = [] { return false; };
        oked::easymodel_ui::DrawEasyModeling(h);
        ImGui::PopStyleVar(2); ImGui::EndChild(); ImGui::End();
        return;
    }
    std::string sec;
    const float w = ImGui::GetContentRegionAvail().x; float x = 0;
    for (const mt::Tool& t : mt::Tools()) {
        if (t.page != page) continue;
        if (sec != t.section) { sec = t.section; ImGui::Spacing(); ImGui::TextColored(ImVec4(0.9f, 0.92f, 0.97f, 1), "%s", t.section); x = 0; }
        const ImVec2 sz = ImGui::CalcTextSize(t.name);
        const float bw = sz.x + 24;
        if (x > 0 && x + bw > w) x = 0; else if (x > 0) ImGui::SameLine();
        ImGui::Button((std::string(t.name) + "##" + t.id).c_str());
        if (mt::Score(t, query) > 0 && !query.empty()) { const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2), IM_COL32(255, 205, 60, 255), 4.0f, 0, 2.5f); }
        x += bw + ImGui::GetStyle().ItemSpacing.x;
    }
    ImGui::PopStyleVar(2); ImGui::EndChild(); ImGui::End();
}
int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : ".";
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* win = SDL_CreateWindow("shot", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
    SDL_Renderer* rd = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr;
    if (argc > 2) io.Fonts->AddFontFromFileTTF(argv[2], 16.0f);
    ImGui_ImplSDL2_InitForSDLRenderer(win, rd); ImGui_ImplSDLRenderer2_Init(rd);
    struct S { const char* name; int page; const char* q; const char* hint; bool folded; };
    const S shots[] = {{"10_easy_page_unfolded", 8, "", "", false}, {"11_easy_page_folded", 8, "", "", true}, {"12_fix_the_surface_unfolded", 5, "", "", false},
                       {"13_fix_the_surface_folded", 5, "", "", true}, {"14_search_round", 8, "round", "", false}, {"15_add_shapes_narrow_text", 0, "", "", false}};
    for (const S& s : shots) for (int f = 0; f < 3; ++f) {
        ImGui_ImplSDLRenderer2_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        Draw(s.page, s.q, s.hint, s.folded); ImGui::Render();
        SDL_SetRenderDrawColor(rd, 30, 30, 34, 255); SDL_RenderClear(rd);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), rd);
        if (f == 2) Save(rd, 1280, 720, out + "/" + s.name + ".bmp");
        SDL_RenderPresent(rd);
    }
    return 0;
}
