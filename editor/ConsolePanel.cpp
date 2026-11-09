// The Console panel. Every tool writes with ConsoleLog; the window draws
// ConsoleModel::Rows() so filters, collapse and the source link are the model's.
#include "EditorCommon.hpp"
#include "ConsolePanel.hpp"
#include "EditorWidgets.hpp"
#include "MainShared.hpp"
#include "ProblemsPanel.hpp"

#include <cstdlib>
#include <ctime>
#include <set>

namespace oked {

int g_consoleCounts[3] = {0, 0, 0};

namespace {

okay::ConsoleModel g_model(2000);
bool g_clockSet = false;
bool g_errorPause = false;
std::string g_preferCategory;
bool g_preferPending = false;

okay::Log::Level ToLevel(int level) {
    if (level >= 2) return okay::Log::Level::Error;
    if (level == 1) return okay::Log::Level::Warning;
    return okay::Log::Level::Info;
}

int FromLevel(okay::Log::Level level) {
    if (level == okay::Log::Level::Error) return 2;
    if (level == okay::Log::Level::Warning) return 1;
    return 0;
}

void RefreshCounts() {
    okay::ConsoleCounts c = g_model.Counts();
    g_consoleCounts[0] = (int)(c.info + c.trace);
    g_consoleCounts[1] = (int)c.warning;
    g_consoleCounts[2] = (int)c.error;
}

double WallClock() {
    std::time_t t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    if (!lt) return 0.0;
    return lt->tm_hour * 3600.0 + lt->tm_min * 60.0 + lt->tm_sec;
}

std::string ClockText(double sec) {
    int s = (int)sec;
    if (s < 0) s = 0;
    char b[16];
    std::snprintf(b, sizeof(b), "%02d:%02d:%02d", (s / 3600) % 24, (s / 60) % 60, s % 60);
    return b;
}

// A leading `[tag] ` is a category. Tags with ':' or '/' are source references.
bool SplitCategory(const std::string& msg, std::string& category, std::string& rest) {
    if (msg.size() < 3 || msg[0] != '[') return false;
    std::size_t close = msg.find(']');
    if (close == std::string::npos || close < 2 || close > 33) return false;
    for (std::size_t i = 1; i < close; ++i) {
        char c = msg[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    category = msg.substr(1, close - 1);
    std::size_t r = close + 1;
    if (r < msg.size() && msg[r] == ' ') ++r;
    rest = msg.substr(r);
    return true;
}

void MergeSource(okay::ConsoleSource& src, const okay::ConsoleSource& extra) {
    if (src.kind == okay::ConsoleSource::Kind::None && extra.kind != okay::ConsoleSource::Kind::None)
        src.kind = extra.kind;
    else if (extra.kind == okay::ConsoleSource::Kind::Object || extra.kind == okay::ConsoleSource::Kind::Script ||
             extra.kind == okay::ConsoleSource::Kind::Graph)
        src.kind = extra.kind;
    if (!extra.path.empty()) src.path = extra.path;
    if (!extra.guid.empty()) src.guid = extra.guid;
    if (!extra.component.empty()) src.component = extra.component;
    if (!extra.nodeId.empty()) src.nodeId = extra.nodeId;
    if (extra.line) src.line = extra.line;
    if (extra.column) src.column = extra.column;
}

std::string RowLabel(const okay::ConsoleRow& row) {
    const okay::ConsoleEntry& e = row.entry;
    std::string label = "     " + ClockText(e.timeSec) + "  ";
    if (!e.category.empty()) label += "[" + e.category + "] ";
    label += e.message;
    if (row.repeat > 1) label += "  (" + std::to_string(row.repeat) + ")";
    return label;
}

} // namespace

void ConsolePreferCategory(const std::string& category) {
    g_preferCategory = category;
    g_preferPending = true;
}

void ConsoleLog(const std::string& msg, int level) {
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    if (!g_clockSet) { g_model.SetClock(WallClock); g_clockSet = true; }
    std::string category, text = msg;
    std::string cat, rest;
    if (SplitCategory(msg, cat, rest)) { category = std::move(cat); text = std::move(rest); }
    okay::ConsoleSource src = okay::ParseConsoleSource(text);
    MergeSource(src, okay::ActiveConsoleSource());
    g_model.SetPauseOnError(g_errorPause);
    g_model.Add(ToLevel(level), text, category, src);
    if (g_model.ConsumePauseRequest()) g_paused = true;
    RefreshCounts();
}

void ConsoleClear() {
    g_model.Clear();
    RefreshCounts();
}

void DrawConsole() {
    static char filter[96] = "";
    static bool autoScroll = true;
    static bool collapse = false;
    static bool showInfo = true, showWarn = true, showError = true;
    static std::uint64_t selectedSeq = ~0ull;
    static int catSel = 0;

    char conTitle[64];
    if (g_consoleCounts[2] > 0)
        std::snprintf(conTitle, sizeof(conTitle), "Console  [x %d]###Console", g_consoleCounts[2]);
    else if (g_consoleCounts[1] > 0)
        std::snprintf(conTitle, sizeof(conTitle), "Console  [! %d]###Console", g_consoleCounts[1]);
    else
        std::snprintf(conTitle, sizeof(conTitle), "Console###Console");
    g_model.SetPauseOnError(g_errorPause);
    if (g_model.ConsumePauseRequest()) g_paused = true;

    if (ImGui::Begin(conTitle, &g_showConsole)) {
        const ImVec4 cInfo(0.82f, 0.84f, 0.88f, 1.0f);
        const ImVec4 cWarn(0.95f, 0.80f, 0.35f, 1.0f);
        const ImVec4 cErr (0.96f, 0.45f, 0.42f, 1.0f);
        const ImGuiStyle& cst = ImGui::GetStyle();

        std::vector<std::string> cats;
        cats.push_back("All");
        {
            std::set<std::string> seen;
            for (const okay::ConsoleEntry& e : g_model.Entries())
                if (!e.category.empty() && seen.insert(e.category).second) cats.push_back(e.category);
        }
        if (g_preferPending) {
            catSel = 0;
            for (int i = 1; i < (int)cats.size(); ++i)
                if (cats[i] == g_preferCategory) { catSel = i; break; }
            g_preferPending = false;
        }
        if (catSel < 0 || catSel >= (int)cats.size()) catSel = 0;

        if (IconButton("##cclear", TbIcon::Clear, "Clear the console")) { ConsoleClear(); selectedSeq = ~0ull; }
        ImGui::SameLine();
        const float catW = 128.0f;
        auto pillW = [&](int count) {
            char b[16]; std::snprintf(b, sizeof(b), "%d", count);
            return ImGui::CalcTextSize(b).x + ImGui::GetFontSize() * 0.9f + cst.FramePadding.x * 2.0f;
        };
        float pillsW = pillW(g_consoleCounts[0]) + pillW(g_consoleCounts[1]) + pillW(g_consoleCounts[2]) +
                       cst.ItemSpacing.x * 0.5f * 2.0f;
        float moreW = ImGui::GetFrameHeight();
        SearchField("##cfilter", filter, sizeof(filter), "Search log",
                    -(pillsW + moreW + catW + cst.ItemSpacing.x * 3.0f));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(catW);
        if (ImGui::BeginCombo("##ccat", cats[catSel].c_str())) {
            for (int i = 0; i < (int)cats.size(); ++i) {
                if (ImGui::Selectable(cats[i].c_str(), catSel == i)) catSel = i;
                if (catSel == i) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show one category");

        auto pill = [&](const char* id, bool& on, const ImVec4& col, int count, const char* tip) {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%d%s", count, id);
            float dotR = ImGui::GetFontSize() * 0.2f;
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(1.0f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(on ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg));
            ImGui::PushStyleColor(ImGuiCol_Text, on ? col : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Button(buf, ImVec2(pillW(count), 0))) on = !on;
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            ImVec2 mn = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(mn.x + cst.FramePadding.x + dotR, (mn.y + ImGui::GetItemRectMax().y) * 0.5f), dotR,
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
            ImGui::MenuItem("Pause on error", nullptr, &g_errorPause);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause Play mode whenever a new error is logged");
            ImGui::Separator();
            if (ImGui::MenuItem("Copy all")) ImGui::SetClipboardText(g_model.CopyAll().c_str());
            if (ImGui::MenuItem("Save to console_log.txt")) {
                std::ofstream lf("console_log.txt");
                lf << g_model.CopyAll(false);
                ConsoleLog("Console saved to console_log.txt");
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();

        okay::ConsoleFilter f;
        f.levelMask = 0;
        if (showInfo) f.levelMask |= okay::ConsoleModel::LevelBit(okay::Log::Level::Info) |
                                     okay::ConsoleModel::LevelBit(okay::Log::Level::Trace);
        if (showWarn) f.levelMask |= okay::ConsoleModel::LevelBit(okay::Log::Level::Warning);
        if (showError) f.levelMask |= okay::ConsoleModel::LevelBit(okay::Log::Level::Error);
        f.text = filter;
        f.category = catSel == 0 ? std::string() : cats[catSel];
        g_model.SetFilter(f);
        g_model.SetCollapse(collapse);
        const std::vector<okay::ConsoleRow> rows = g_model.Rows();

        const ImVec4 levelCol[3] = {cInfo, cWarn, cErr};
        float detailsH = 90.0f;
        ImGui::BeginChild("log", ImVec2(0, -detailsH));
        ImGuiListClipper clipper;
        clipper.Begin((int)rows.size());
        const okay::ConsoleRow* selected = nullptr;
        while (clipper.Step())
        for (int vi = clipper.DisplayStart; vi < clipper.DisplayEnd; ++vi) {
            const okay::ConsoleRow& row = rows[(std::size_t)vi];
            const okay::ConsoleEntry& e = row.entry;
            const int lvl = FromLevel(e.level);
            ImVec2 rp = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_Text, levelCol[lvl]);
            std::string label = RowLabel(row) + "##c" + std::to_string(e.seq);
            const bool isSel = selectedSeq == e.seq;
            if (ImGui::Selectable(label.c_str(), isSel, 0, ImVec2(0, ImGui::GetTextLineHeight() + 10.0f))) {
                selectedSeq = e.seq;
                if (e.source.Valid()) ActivateBoundConsoleSource(e.source);
            }
            if (selectedSeq == e.seq) selected = &row;
            {
                float dr = ImGui::GetFontSize() * 0.22f;
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(rp.x + dr + 7.0f, rp.y + ImGui::GetTextLineHeight() * 0.55f),
                    dr, ImGui::GetColorU32(levelCol[lvl]));
            }
            if (ImGui::BeginPopupContextItem()) {
                selectedSeq = e.seq;
                if (ImGui::MenuItem("Copy Entry"))
                    ImGui::SetClipboardText((ClockText(e.timeSec) + "  " + e.message).c_str());
                if (ImGui::MenuItem("Copy All Visible"))
                    ImGui::SetClipboardText(g_model.CopyAll().c_str());
                ImGui::EndPopup();
            }
            ImVec2 rmn = ImGui::GetItemRectMin(), rmx = ImGui::GetItemRectMax();
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            if (lvl > 0) {
                const ImVec4& lc = levelCol[lvl];
                cdl->AddRectFilled(rmn, ImVec2(rmx.x, rmx.y),
                                   ImGui::GetColorU32(ImVec4(lc.x, lc.y, lc.z, lvl == 2 ? 0.09f : 0.05f)));
            }
            cdl->AddRectFilled(rmn, ImVec2(rmn.x + 3.0f, rmx.y), ImGui::GetColorU32(levelCol[lvl]));
            ImGui::PopStyleColor();
        }
        clipper.End();
        if (!selected)
            for (const okay::ConsoleRow& row : rows)
                if (row.entry.seq == selectedSeq) { selected = &row; break; }
        if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::BeginChild("details", ImVec2(0, 0));
        if (selected) {
            const okay::ConsoleEntry& e = selected->entry;
            const int lvl = FromLevel(e.level);
            ImGui::PushStyleColor(ImGuiCol_Text, levelCol[lvl]);
            ImGui::TextWrapped("%s", e.message.c_str());
            ImGui::PopStyleColor();
            std::string when = ClockText(e.timeSec);
            if (!e.category.empty())
                ImGui::TextDisabled("%s  [%s]  at %s", lvl == 2 ? "Error" : lvl == 1 ? "Warning" : "Info",
                                    e.category.c_str(), when.c_str());
            else
                ImGui::TextDisabled("%s at %s", lvl == 2 ? "Error" : lvl == 1 ? "Warning" : "Info", when.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##cdet")) ImGui::SetClipboardText(e.message.c_str());
            std::string spath;
            int sline = e.source.line;
            if (e.source.kind == okay::ConsoleSource::Kind::Script || e.source.kind == okay::ConsoleSource::Kind::Asset)
                spath = e.source.path;
            if (spath.empty()) {
                for (const char* ex : {".okay", ".lua", ".cs"}) {
                    std::size_t p = e.message.find(ex);
                    if (p == std::string::npos) continue;
                    std::size_t start = e.message.find_last_of(" \t'\"(,;", p);
                    start = start == std::string::npos ? 0 : start + 1;
                    spath = e.message.substr(start, p + std::strlen(ex) - start);
                    break;
                }
            }
            if (sline <= 0)
                if (std::size_t lp = e.message.find("line "); lp != std::string::npos)
                    sline = std::atoi(e.message.c_str() + lp + 5);
            bool scriptish = false;
            for (const char* ex : {".okay", ".lua", ".cs"})
                if (spath.size() >= std::strlen(ex) && spath.compare(spath.size() - std::strlen(ex), std::strlen(ex), ex) == 0)
                    scriptish = true;
            if (scriptish && !spath.empty()) {
                ImGui::SameLine();
                char ob[64];
                std::snprintf(ob, sizeof(ob), sline > 0 ? "Open Script (line %d)##cdet" : "Open Script##cdet", sline);
                if (ImGui::SmallButton(ob)) OpenScriptFileInEditorAt(spath, sline > 0 ? sline : 1);
            }
        } else {
            ImGui::TextDisabled("Select a log entry to see details.");
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace oked
