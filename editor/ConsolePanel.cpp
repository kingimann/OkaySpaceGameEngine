// The Console panel and the editor log every tool writes to (ConsoleLog).
#include "EditorCommon.hpp"
#include "ConsolePanel.hpp"
#include "EditorWidgets.hpp"
#include "MainShared.hpp"

namespace oked {

std::vector<ConsoleEntry> g_console;
int g_consoleCounts[3] = {0, 0, 0};   // running totals per level (for the toggles)

void ConsoleLog(const std::string& msg, int level) {
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    // Collapse an immediate repeat of the same message into a count.
    if (!g_console.empty() && g_console.back().text == msg && g_console.back().level == level) {
        g_console.back().count++;
        g_consoleCounts[level]++;
        return;
    }
    char ts[16];
    std::time_t t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d", lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0);
    g_console.push_back({level, msg, ts, 1});
    g_consoleCounts[level]++;
    if (g_console.size() > 500) g_console.erase(g_console.begin());
}
void ConsoleClear() {
    g_console.clear();
    g_consoleCounts[0] = g_consoleCounts[1] = g_consoleCounts[2] = 0;
}

// A console: severity icons + colors, Collapse, per-level filter
// toggles with counts, search, a selectable list, and a details pane.
void DrawConsole() {
    static char filter[96] = "";
    static bool autoScroll = true;
    static bool collapse = false;
    static bool showInfo = true, showWarn = true, showError = true;
    static int  selected = -1;

    // Badge the tab with the current error/warning counts so problems are
    // visible even when the Console is behind another tab. The ### keeps the
    // window ID (and docking) stable while the visible label changes.
    char conTitle[64];
    if (g_consoleCounts[2] > 0)
        std::snprintf(conTitle, sizeof(conTitle), "Console  [x %d]###Console", g_consoleCounts[2]);
    else if (g_consoleCounts[1] > 0)
        std::snprintf(conTitle, sizeof(conTitle), "Console  [! %d]###Console", g_consoleCounts[1]);
    else
        std::snprintf(conTitle, sizeof(conTitle), "Console###Console");
    static bool s_errorPause = false;
    static int  s_lastErrCount = 0;
    if (s_errorPause && g_consoleCounts[2] > s_lastErrCount) g_paused = true;   // halt Play on new errors
    s_lastErrCount = g_consoleCounts[2];
    if (ImGui::Begin(conTitle, &g_showConsole)) {
        // One clean row: [Clear] [search..........] [● 3][● 1][● 0] [...]
        // The level pills filter the log; the rarely-changed switches (collapse,
        // clear on play, error pause, auto-scroll) and Save/Copy live in "...".
        const ImVec4 cInfo(0.82f, 0.84f, 0.88f, 1.0f);
        const ImVec4 cWarn(0.95f, 0.80f, 0.35f, 1.0f);
        const ImVec4 cErr (0.96f, 0.45f, 0.42f, 1.0f);
        const ImGuiStyle& cst = ImGui::GetStyle();
        if (IconButton("##cclear", TbIcon::Clear, "Clear the console")) { ConsoleClear(); selected = -1; }
        ImGui::SameLine();
        // Level pill: a coloured dot + count, highlighted while that level shows.
        auto pillW = [&](int count) {
            char b[16]; std::snprintf(b, sizeof(b), "%d", count);
            return ImGui::CalcTextSize(b).x + ImGui::GetFontSize() * 0.9f + cst.FramePadding.x * 2.0f;
        };
        float pillsW = pillW(g_consoleCounts[0]) + pillW(g_consoleCounts[1]) + pillW(g_consoleCounts[2]) +
                       cst.ItemSpacing.x * 0.5f * 2.0f;
        float moreW = ImGui::GetFrameHeight();
        SearchField("##cfilter", filter, sizeof(filter), "Search log",
                    -(pillsW + moreW + cst.ItemSpacing.x * 2.0f));
        auto pill = [&](const char* id, bool& on, const ImVec4& col, int count, const char* tip) {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%d%s", count, id);
            float dotR = ImGui::GetFontSize() * 0.2f;
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(1.0f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(on ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg));
            ImGui::PushStyleColor(ImGuiCol_Text, on ? col : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Button(buf, ImVec2(pillW(count), 0))) on = !on;
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(mn.x + cst.FramePadding.x + dotR, (mn.y + mx.y) * 0.5f), dotR,
                ImGui::GetColorU32(on ? col : ImVec4(col.x, col.y, col.z, 0.35f)));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        };
        ImGui::SameLine();
        pill("##cinfo", showInfo,  cInfo, g_consoleCounts[0], "Show / hide messages");
        ImGui::SameLine(0, cst.ItemSpacing.x * 0.5f);
        pill("##cwarn", showWarn,  cWarn, g_consoleCounts[1], "Show / hide warnings");
        ImGui::SameLine(0, cst.ItemSpacing.x * 0.5f);
        pill("##cerr",  showError, cErr,  g_consoleCounts[2], "Show / hide errors");
        ImGui::SameLine();
        if (IconButton("##cmore", TbIcon::More, "Console options")) ImGui::OpenPopup("##conopts");
        if (ImGui::BeginPopup("##conopts")) {
            ImGui::MenuItem("Collapse duplicates", nullptr, &collapse);
            ImGui::MenuItem("Auto-scroll", nullptr, &autoScroll);
            ImGui::MenuItem("Clear on Play", nullptr, &g_clearConsoleOnPlay);
            ImGui::MenuItem("Pause on error", nullptr, &s_errorPause);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause Play mode whenever a new error is logged");
            ImGui::Separator();
            if (ImGui::MenuItem("Copy all")) {
                std::string all;
                for (const auto& e : g_console)
                    all += std::string(e.level == 2 ? "[x] " : e.level == 1 ? "[!] " : "[i] ") +
                           e.time + "  " + e.text + "\n";
                ImGui::SetClipboardText(all.c_str());
            }
            if (ImGui::MenuItem("Save to console_log.txt")) {   // dump the whole log beside the exe
                std::ofstream lf("console_log.txt");
                for (const auto& e : g_console)
                    lf << (e.level == 2 ? "[x] " : e.level == 1 ? "[!] " : "[i] ")
                       << e.time << "  " << e.text << "\n";
                ConsoleLog("Console saved to console_log.txt");
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();

        std::string needle = filter;
        for (auto& n : needle) n = (char)std::tolower((unsigned char)n);
        const ImVec4 levelCol[3] = {cInfo, cWarn, cErr};
        const bool   levelShow[3] = {showInfo, showWarn, showError};

        // Details pane reserves the bottom; the list fills the rest.
        float detailsH = 90.0f;
        ImGui::BeginChild("log", ImVec2(0, -detailsH));
        // Optional collapse: merge identical (level,text) lines, summing counts.
        std::vector<int> order;          // indices into g_console to display
        std::vector<int> mergedCount;    // parallel display counts
        if (collapse) {
            std::vector<int> firstAt;    // first display index for a (level|text)
            std::unordered_map<std::string, int> seen;
            for (int i = 0; i < (int)g_console.size(); ++i) {
                const auto& e = g_console[i];
                std::string key = std::to_string(e.level) + "|" + e.text;
                auto it = seen.find(key);
                if (it == seen.end()) { seen[key] = (int)order.size(); order.push_back(i); mergedCount.push_back(e.count); }
                else mergedCount[it->second] += e.count;
            }
        } else {
            for (int i = 0; i < (int)g_console.size(); ++i) { order.push_back(i); mergedCount.push_back(g_console[i].count); }
        }

        // Pre-filter to the rows that actually show, so an ImGuiListClipper can
        // skip the offscreen ones instead of building a label + Selectable for
        // every entry each frame (the cost that grew with the log size).
        std::vector<int> visible;
        visible.reserve(order.size());
        for (std::size_t row = 0; row < order.size(); ++row) {
            const ConsoleEntry& e = g_console[order[row]];
            if (!levelShow[e.level]) continue;
            if (!needle.empty()) {
                std::string low = e.text;
                for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
                if (low.find(needle) == std::string::npos) continue;
            }
            visible.push_back((int)row);
        }
        ImGuiListClipper clipper;
        clipper.Begin((int)visible.size());
        while (clipper.Step())
        for (int vi = clipper.DisplayStart; vi < clipper.DisplayEnd; ++vi) {
            std::size_t row = (std::size_t)visible[vi];
            int i = order[row];
            const ConsoleEntry& e = g_console[i];
            // Drawn level dot in place of the old [i]/[!]/[x] text markers (the
            // severity bar + row wash below already tint the row itself).
            ImVec2 rp = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_Text, levelCol[e.level]);
            std::string label = "     " + e.time + "  " + e.text;   // leading room for the dot
            if (mergedCount[row] > 1) label += "  (" + std::to_string(mergedCount[row]) + ")";
            label += "##c" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selected == i)) selected = i;
            {
                float dr = ImGui::GetFontSize() * 0.22f;
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(rp.x + dr + 7.0f, rp.y + ImGui::GetTextLineHeight() * 0.55f),
                    dr, ImGui::GetColorU32(levelCol[e.level]));
            }
            // Right-click an entry: copy it (or everything currently visible).
            if (ImGui::BeginPopupContextItem()) {
                selected = i;
                if (ImGui::MenuItem("Copy Entry"))
                    ImGui::SetClipboardText((e.time + "  " + e.text).c_str());
                if (ImGui::MenuItem("Copy All Visible")) {
                    std::string all;
                    for (std::size_t r2 = 0; r2 < order.size(); ++r2) {
                        const ConsoleEntry& e2 = g_console[order[r2]];
                        if (!levelShow[e2.level]) continue;
                        if (!needle.empty()) {
                            std::string lo = e2.text;
                            for (auto& ch : lo) ch = (char)std::tolower((unsigned char)ch);
                            if (lo.find(needle) == std::string::npos) continue;
                        }
                        all += (e2.level == 2 ? "[x] " : e2.level == 1 ? "[!] " : "[i] ");
                        all += e2.time + "  " + e2.text + "\n";
                    }
                    ImGui::SetClipboardText(all.c_str());
                }
                ImGui::EndPopup();
            }
            // A severity color-bar down the left edge, plus a faint row wash for
            // warnings/errors so they stand out when scanning a busy log.
            ImVec2 rmn = ImGui::GetItemRectMin(), rmx = ImGui::GetItemRectMax();
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            if (e.level > 0) {
                const ImVec4& lc = levelCol[e.level];
                cdl->AddRectFilled(rmn, ImVec2(rmx.x, rmx.y),
                                   ImGui::GetColorU32(ImVec4(lc.x, lc.y, lc.z, e.level == 2 ? 0.09f : 0.05f)));
            }
            cdl->AddRectFilled(rmn, ImVec2(rmn.x + 3.0f, rmx.y), ImGui::GetColorU32(levelCol[e.level]));
            ImGui::PopStyleColor();
        }
        clipper.End();
        if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::BeginChild("details", ImVec2(0, 0));
        if (selected >= 0 && selected < (int)g_console.size()) {
            const ConsoleEntry& e = g_console[selected];
            ImGui::PushStyleColor(ImGuiCol_Text, levelCol[e.level]);
            ImGui::TextWrapped("%s", e.text.c_str());
            ImGui::PopStyleColor();
            ImGui::TextDisabled("%s at %s", e.level == 2 ? "Error" : e.level == 1 ? "Warning" : "Info", e.time.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##cdet")) ImGui::SetClipboardText(e.text.c_str());
            // If the message references a script file, offer to jump straight to it
            // (and to "line N" when the message carries one).
            {
                std::string spath; int sline = 0;
                for (const char* ex : {".okay", ".lua", ".cs"}) {
                    std::size_t p = e.text.find(ex);
                    if (p == std::string::npos) continue;
                    // Walk back to the start of the path token.
                    std::size_t start = e.text.find_last_of(" \t'\"(,;", p);
                    start = start == std::string::npos ? 0 : start + 1;
                    spath = e.text.substr(start, p + std::strlen(ex) - start);
                    break;
                }
                if (std::size_t lp = e.text.find("line "); lp != std::string::npos)
                    sline = std::atoi(e.text.c_str() + lp + 5);
                if (!spath.empty()) {
                    ImGui::SameLine();
                    char ob[48];
                    std::snprintf(ob, sizeof(ob), sline > 0 ? "Open Script (line %d)##cdet" : "Open Script##cdet", sline);
                    if (ImGui::SmallButton(ob)) OpenScriptFileInEditorAt(spath, sline);
                }
            }
        } else {
            ImGui::TextDisabled("Select a log entry to see details.");
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace oked
