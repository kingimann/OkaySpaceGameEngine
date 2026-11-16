// Draws the launcher's version question (OpenProjectUI.hpp). The layout is the editor's own window for the same question
// (editor/ProjectUpgradeUI.cpp, DrawGatePrompt): a small heading, the question, the project's folder, a short explanation, the status
// of the old editor and the buttons in a row. The words come from OpenProjectGate.hpp (shared/ProjectQuestion.hpp), so both read the same.
// Written for this engine from that window and Dear ImGui's documented API; no outside code.
#include "OpenProjectUI.hpp"

#include <algorithm>
#include <string>

namespace okayopenui {

namespace {

const ImVec4 kAmber(0.95f, 0.80f, 0.35f, 1.0f), kGreen(0.45f, 0.82f, 0.56f, 1.0f);

// A long path keeps its end (the part that differs), as the editor's window does.
std::string FitEnd(const std::string& s, float maxW) {
    if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
    const std::string dots = "\xE2\x80\xA6";
    const float dotsW = ImGui::CalcTextSize(dots.c_str()).x;
    std::size_t cut = 0;
    while (cut < s.size()) {
        for (int n = 0; n < 3 && cut < s.size(); ++n) {   // three characters at a time (whole UTF-8 characters)
            ++cut;
            while (cut < s.size() && ((unsigned char)s[cut] & 0xC0) == 0x80) ++cut;
        }
        if (dotsW + ImGui::CalcTextSize(s.c_str() + cut).x <= maxW) break;
    }
    return dots + (s.c_str() + cut);
}

// The launcher's primary button (accent colour, white text), as its PrimaryButton in main.cpp draws it.
bool Primary(const char* label, const ImVec2& size, const Look& look) {
    auto lift = [](float x) { return x + (1.0f - x) * 0.20f; };
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(look.accent.x, look.accent.y, look.accent.z, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(lift(look.accent.x), lift(look.accent.y), lift(look.accent.z), 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, look.accentDim);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool hit = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return hit;
}

void Tip(const std::string& text) {
    if (text.empty() || !ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

}  // namespace

const char* PopupName() { return "Open project##versionquestion"; }

std::optional<okayopen::Answer> Draw(const okayopen::Prompt& p, const Look& look) {
    if (p.question == okayopen::Question::None) return std::nullopt;
    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::clamp(vp.x - 48.0f, 340.0f, 620.0f), 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal(PopupName(), nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
        return std::nullopt;
    std::optional<okayopen::Answer> answer;
    const float textW = ImGui::GetContentRegionAvail().x;
    ImGui::TextDisabled("%s", p.eyebrow.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, p.warn ? kAmber : ImVec4(1, 1, 1, 1));
    ImGui::TextWrapped("%s", p.heading.c_str());
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("%s", FitEnd(p.root, textW).c_str());
    ImGui::Dummy(ImVec2(0, 4));
    for (std::size_t i = 0; i < p.paragraphs.size(); ++i) {
        if (i) ImGui::Dummy(ImVec2(0, 2));
        ImGui::TextWrapped("%s", p.paragraphs[i].c_str());
    }
    if (!p.status.empty()) ImGui::TextColored(p.statusGood ? kGreen : kAmber, "%s", p.status.c_str());
    ImGui::Dummy(ImVec2(0, 8));
    // The buttons, each as wide as its text, on one line while they fit and on the next when they do not.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float rowRight = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - st.WindowPadding.x;
    bool first = true;
    for (const okayopen::Option& o : p.options) {
        const float w = ImGui::CalcTextSize(o.label.c_str(), nullptr, true).x + st.FramePadding.x * 2.0f + 14.0f;
        if (!first && ImGui::GetItemRectMax().x + st.ItemSpacing.x + w <= rowRight) ImGui::SameLine(0, st.ItemSpacing.x);
        first = false;
        const ImVec2 size(w, 32.0f);
        const bool hit = o.primary ? Primary(o.label.c_str(), size, look) : ImGui::Button(o.label.c_str(), size);
        Tip(o.tip);
        if (hit) answer = o.answer;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) answer = okayopen::Answer::Cancel;
    if (answer) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return answer;
}

}  // namespace okayopenui
