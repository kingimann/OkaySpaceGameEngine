#include "OkayUIScreens.hpp"
#include "ConsoleScriptLink.hpp"   // shared/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace UI = OkayUI;

namespace okayuiscreens {

namespace {

const unsigned char kGreen[3] = {115, 209, 143};
const unsigned char kRed[3]   = {240, 128, 128};
const unsigned char kAmber[3] = {242, 204, 89};
const unsigned char kInfo[3]  = {209, 214, 224};

// A bullet followed by wrapped text: the bullet stays on the first line.
void BulletWrapped(const char* text) {
    UI::Bullet();
    UI::Indent(UI::GetFontSize() * 0.35f + 8.0f);   // the wrapped lines line up under the first one
    UI::SameLine();
    UI::TextWrapped(text);
    UI::Unindent(UI::GetFontSize() * 0.35f + 8.0f);
}

} // namespace

// ---- What's New ------------------------------------------------------------------------------------------------------------

WhatsNewResult DrawWhatsNew(const WhatsNewModel& m, float x, float y, float w, float h) {
    WhatsNewResult res;
    UI::BareNextWindow();
    if (UI::Begin("What's New##okayui", x, y, w, h)) {
        UI::Heading(WhatsNewTitle());
        UI::TextDisabled(WhatsNewSubtitle());
        UI::Spacing(4.0f);
        UI::BeginChild("##whatsnew", 0.0f, UI::GetContentRegionAvail().y, false);

        // ---- your version and the update status ----
        UI::BeginCard("##wnver");
        char line[160];
        UI::PushTextScale(1.12f);
        std::snprintf(line, sizeof(line), "You're on v%s", m.currentVersion.c_str());
        UI::Text(line);
        UI::PopTextScale();
        const bool busy = m.state == UpdateState::Checking;
        switch (m.state) {
        case UpdateState::Available:
            std::snprintf(line, sizeof(line), "v%s is ready to install.", m.availableVersion.c_str());
            UI::TextColored(kGreen[0], kGreen[1], kGreen[2], line);
            break;
        case UpdateState::Checking:
            UI::TextDisabled(m.message.empty() ? "Checking for updates..." : m.message.c_str());
            break;
        case UpdateState::Installed:
            UI::TextColored(kGreen[0], kGreen[1], kGreen[2], "Update installed.");
            break;
        case UpdateState::Failed:
            UI::TextColored(kRed[0], kRed[1], kRed[2], m.message.c_str());
            break;
        case UpdateState::UpToDate:
            UI::TextDisabled("You're up to date.");
            break;
        case UpdateState::Idle:
            UI::TextDisabled("Updates are checked automatically while the launcher is open.");
            break;
        }
        if (m.state == UpdateState::Available) {
            if (UI::PrimaryButton("Install now")) res.installNow = true;
            res.updateButton = UI::GetItemRect();
        } else if (m.state == UpdateState::Installed && m.restartNeeded) {
            if (UI::PrimaryButton("Restart now")) res.restartNow = true;
            res.updateButton = UI::GetItemRect();
        } else {
            UI::BeginDisabled(busy);
            if (UI::Button(busy ? "Checking..." : "Check for updates")) res.checkForUpdates = true;
            res.updateButton = UI::GetItemRect();
            UI::EndDisabled();
        }
        res.updateButtonEnabled = !busy;
        UI::EndCard();
        UI::Spacing(4.0f);

        // ---- the releases: the newest in a card, the rest folded under "Earlier releases" ----
        auto releaseCard = [&](const Release& r, bool latest, int index) {
            UI::PushID(index);
            char id[48];
            std::snprintf(id, sizeof(id), "##rel%d", index);
            UI::BeginCard(id);
            UI::PushTextScale(1.12f);
            UI::Text(r.version.c_str());
            UI::PopTextScale();
            if (latest) { UI::SameLine(); UI::Badge("Latest"); }
            for (const std::string& n : r.notes) BulletWrapped(n.c_str());
            UI::EndCard();
            UI::PopID();
        };
        if (!m.releases.empty()) {
            releaseCard(m.releases[0], !m.placeholder, 0);
            if (m.placeholder)
                UI::TextDisabled("Notes for each release show up here once the launcher has checked for updates.");
            if (m.releases.size() > 1) {
                UI::Spacing(4.0f);
                UI::SeparatorText("Earlier releases");
                for (std::size_t i = 1; i < m.releases.size(); ++i) {
                    UI::PushID((int)i);
                    const bool open = UI::CollapsingHeader(m.releases[i].version.c_str(), i == 1);
                    if (i == 1) res.firstEarlierHeader = UI::GetItemRect();
                    if (open) {
                        UI::Indent(8.0f);
                        for (const std::string& n : m.releases[i].notes) BulletWrapped(n.c_str());
                        UI::Unindent(8.0f);
                    }
                    UI::PopID();
                }
            }
        }
        UI::Spacing(4.0f);
        if (UI::Button("View all releases on GitHub")) res.openReleasesPage = true;
        res.releasesButton = UI::GetItemRect();
        UI::EndChild();
    }
    UI::End();
    return res;
}

// ---- Console ----------------------------------------------------------------------------------------------------------------

std::string ConsoleClockText(double sec) {
    int s = (int)sec;
    if (s < 0) s = 0;
    char b[16];
    std::snprintf(b, sizeof(b), "%02d:%02d:%02d", (s / 3600) % 24, (s / 60) % 60, s % 60);
    return b;
}

namespace {

int LevelIndex(okay::Log::Level l) {
    if (l == okay::Log::Level::Error) return 2;
    if (l == okay::Log::Level::Warning) return 1;
    return 0;
}

std::string RowText(const okay::ConsoleRow& row) {
    const okay::ConsoleEntry& e = row.entry;
    std::string s = ConsoleClockText(e.timeSec) + "  ";
    if (!e.category.empty()) s += "[" + e.category + "] ";
    s += e.message;
    if (row.repeat > 1) s += "  (" + std::to_string(row.repeat) + ")";
    return s;
}

} // namespace

ConsoleResult DrawConsole(okay::ConsoleModel& model, ConsoleViewState& st, const ConsoleHost& host,
                          float x, float y, float w, float h) {
    ConsoleResult res;
    res.saveHint = "console_log.txt";
    const unsigned char* levelCol[3] = {kInfo, kAmber, kRed};

    // The category list, "All" first, then each category in the log once.
    std::vector<std::string> cats;
    cats.push_back("All");
    {
        std::set<std::string> seen;
        for (const okay::ConsoleEntry& e : model.Entries())
            if (!e.category.empty() && seen.insert(e.category).second) cats.push_back(e.category);
    }
    if (!st.preferCategory.empty()) {
        st.category = 0;
        for (int i = 1; i < (int)cats.size(); ++i)
            if (cats[(std::size_t)i] == st.preferCategory) { st.category = i; break; }
        st.preferCategory.clear();
    }
    if (st.category < 0 || st.category >= (int)cats.size()) st.category = 0;
    std::vector<const char*> catItems;
    for (const std::string& c : cats) catItems.push_back(c.c_str());

    const okay::ConsoleCounts counts = model.Counts();

    UI::BareNextWindow();
    if (UI::Begin("Console##okayui", x, y, w, h)) {
        // ---- row 1: clear, search, category ----
        if (UI::Button("Clear")) {
            model.Clear();
            st.selectedSeq = ~0ull;
            if (host.cleared) host.cleared();
        }
        res.clearButton = UI::GetItemRect();
        UI::SameLine();
        const float comboW = 150.0f + 8.0f + UI::CalcTextSize("Category").x;
        UI::SetNextItemWidth(std::max(140.0f, UI::GetContentRegionAvail().x - comboW - 12.0f));
        UI::InputText("Search", st.filter, (int)sizeof(st.filter));
        res.searchField = UI::GetItemRect();
        UI::SameLine();
        UI::SetNextItemWidth(comboW);
        UI::Combo("Category", catItems.data(), (int)catItems.size(), &st.category);
        res.categoryCombo = UI::GetItemRect();

        // ---- row 2: what to show (with counts) ----
        char b[48];
        std::snprintf(b, sizeof(b), "Messages %zu", counts.info + counts.trace);
        UI::Checkbox(b, &st.showInfo);
        UI::SameLine();
        std::snprintf(b, sizeof(b), "Warnings %zu", counts.warning);
        UI::Checkbox(b, &st.showWarn);
        res.warnToggle = UI::GetItemRect();
        UI::SameLine();
        std::snprintf(b, sizeof(b), "Errors %zu", counts.error);
        UI::Checkbox(b, &st.showError);
        res.errorToggle = UI::GetItemRect();
        UI::SameLine();
        UI::Checkbox("Collapse duplicates", &st.collapse);
        res.collapseToggle = UI::GetItemRect();
        UI::SameLine();
        UI::Checkbox("Auto-scroll", &st.autoScroll);
        res.autoScrollToggle = UI::GetItemRect();
        // ---- row 3: host options and the two actions ----
        bool any = false;
        if (host.clearOnPlay) { UI::Checkbox("Clear on Play", host.clearOnPlay); res.clearOnPlayToggle = UI::GetItemRect(); any = true; }
        if (host.pauseOnError) {
            if (any) UI::SameLine();
            UI::Checkbox("Pause on error", host.pauseOnError);
            res.pauseOnErrorToggle = UI::GetItemRect();
            any = true;
        }
        if (any) UI::SameLine();
        UI::BeginDisabled(!host.copyText);
        if (UI::Button("Copy all") && host.copyText) host.copyText(model.CopyAll());
        res.copyAllButton = UI::GetItemRect();
        UI::EndDisabled();
        UI::SameLine();
        UI::BeginDisabled(!host.saveLog);
        if (UI::Button("Save to console_log.txt") && host.saveLog) host.saveLog(model.CopyAll(false));
        res.saveButton = UI::GetItemRect();
        UI::EndDisabled();
        UI::Separator();

        // ---- the rows ----
        okay::ConsoleFilter f;
        f.levelMask = 0;
        if (st.showInfo) f.levelMask |= okay::ConsoleModel::LevelBit(okay::Log::Level::Info) | okay::ConsoleModel::LevelBit(okay::Log::Level::Trace);
        if (st.showWarn) f.levelMask |= okay::ConsoleModel::LevelBit(okay::Log::Level::Warning);
        if (st.showError) f.levelMask |= okay::ConsoleModel::LevelBit(okay::Log::Level::Error);
        f.text = st.filter;
        f.category = st.category == 0 ? std::string() : cats[(std::size_t)st.category];
        model.SetFilter(f);
        model.SetCollapse(st.collapse);
        const std::vector<okay::ConsoleRow> rows = model.Rows();
        res.rowsShown = (int)rows.size();

        const float detailsH = UI::GetFrameHeight() * 3.0f + 24.0f;
        const float logH = std::max(60.0f, UI::GetContentRegionAvail().y - detailsH - 8.0f);
        const UI::Vec2 logPos = UI::GetCursorScreenPos();
        const float rowH = UI::GetFrameHeight();
        UI::BeginChild("##log", 0.0f, logH, true);
        res.logArea = UI::Rect{logPos.x, logPos.y, UI::GetContentRegionAvail().x, logH};
        const okay::ConsoleRow* selected = nullptr;
        for (const okay::ConsoleRow& row : rows) {
            const okay::ConsoleEntry& e = row.entry;
            // Rows far off screen are a spacer of the same height instead of a widget, so a log of thousands of lines costs
            // what the visible ones cost (the draw batch is bounded).
            const float ry = UI::GetCursorScreenPos().y;
            if (ry + rowH < logPos.y - rowH || ry > logPos.y + logH + rowH) {
                UI::Dummy(1.0f, rowH);
                if (e.seq == st.selectedSeq) selected = &row;
                continue;
            }
            ++res.rowsDrawn;
            if (res.firstDrawnSeq == ~0ull) res.firstDrawnSeq = e.seq;
            res.lastDrawnSeq = e.seq;
            const int lvl = LevelIndex(e.level);
            UI::PushID((int)(e.seq & 0x7fffffff));
            UI::PushStyleColor(UI::Col_Text, levelCol[lvl][0], levelCol[lvl][1], levelCol[lvl][2]);
            const std::string label = RowText(row);
            if (UI::Selectable(label.c_str(), e.seq == st.selectedSeq)) {
                st.selectedSeq = e.seq;
                if (e.source.Valid() && host.activateSource) host.activateSource(e.source);
            }
            UI::PopStyleColor();
            if (res.firstRow.w == 0.0f) res.firstRow = UI::GetItemRect();
            if (UI::BeginPopupContextItem("##rowmenu")) {
                st.selectedSeq = e.seq;
                if (UI::Selectable("Copy Entry", false)) {
                    if (host.copyText) host.copyText(ConsoleClockText(e.timeSec) + "  " + e.message);
                    UI::CloseCurrentPopup();
                }
                if (UI::Selectable("Copy All Visible", false)) {
                    if (host.copyText) host.copyText(model.CopyAll());
                    UI::CloseCurrentPopup();
                }
                UI::EndPopup();
            }
            UI::PopID();
            if (e.seq == st.selectedSeq) selected = &row;
        }
        if (st.autoScroll && UI::GetScrollY() >= UI::GetScrollMaxY() - 1.0f) UI::SetScrollY(1e9f);
        UI::EndChild();

        // ---- the details of the selected entry ----
        UI::Separator();
        if (selected) {
            const okay::ConsoleEntry& e = selected->entry;
            const int lvl = LevelIndex(e.level);
            UI::PushStyleColor(UI::Col_Text, levelCol[lvl][0], levelCol[lvl][1], levelCol[lvl][2]);
            UI::TextWrapped(e.message.c_str());
            UI::PopStyleColor();
            char info[200];
            const char* kind = lvl == 2 ? "Error" : lvl == 1 ? "Warning" : "Info";
            const std::string when = ConsoleClockText(e.timeSec);
            if (!e.category.empty()) std::snprintf(info, sizeof(info), "%s  [%s]  at %s", kind, e.category.c_str(), when.c_str());
            else std::snprintf(info, sizeof(info), "%s at %s", kind, when.c_str());
            UI::TextDisabled(info);
            UI::SameLine();
            UI::BeginDisabled(!host.copyText);
            if (UI::SmallButton("Copy") && host.copyText) host.copyText(e.message);
            res.detailsCopyButton = UI::GetItemRect();
            UI::EndDisabled();
            std::string spath; int sline = 0;
            if (okayconsole::ScriptLink(e, spath, sline) && host.openScript) {
                UI::SameLine();
                char ob[64];
                std::snprintf(ob, sizeof(ob), sline > 0 ? "Open Script (line %d)" : "Open Script", sline);
                if (UI::SmallButton(ob)) host.openScript(spath, sline > 0 ? sline : 1);
                res.openScriptButton = UI::GetItemRect();
            }
        } else {
            UI::TextDisabled("Select a log entry to see details.");
        }
    }
    UI::End();
    return res;
}

} // namespace okayuiscreens
