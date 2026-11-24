#include "OkayUIPanels.hpp"
#include "okay/UI/OkayUICompat.hpp"   // ListClipper, BeginTooltip

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace UI = OkayUI;

namespace okayuiscreens {

namespace {

const unsigned char kGreen[3] = {115, 209, 143};
const unsigned char kRed[3]   = {217, 77, 77};

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

} // namespace

// ---- Tips ------------------------------------------------------------------------------------------------------------------

bool TipMatches(const TipEntry& tip, int cat, const std::string& query) {
    if (cat && tip.cat != cat) return false;
    if (query.empty()) return true;
    const std::string q = Lower(query);
    return Lower(tip.title).find(q) != std::string::npos || Lower(tip.body).find(q) != std::string::npos;
}

TipsResult DrawTips(const TipsModel& m, TipsState& st, float x, float y, float w, float h) {
    TipsResult res;
    const int nCats = (int)m.cats.size();
    const int nTips = (int)m.tips.size();
    if (st.cat < 0 || st.cat >= std::max(nCats, 1)) st.cat = 0;
    st.query[sizeof(st.query) - 1] = '\0';
    if (st.daily < 0 && nTips > 0) st.daily = (int)(((m.dayNumber % nTips) + nTips) % nTips);
    if (st.daily >= nTips) st.daily = 0;
    auto catName = [&](int c) { return c >= 0 && c < nCats ? m.cats[(std::size_t)c].c_str() : ""; };

    UI::BareNextWindow();
    if (UI::Begin("Tips##okayui", x, y, w, h)) {
        UI::Heading("Tips");
        UI::TextDisabled("Shortcuts and tricks for the editor, scripting, building and the launcher.");
        UI::Spacing(4.0f);
        UI::BeginChild("##tipspage", 0.0f, UI::GetContentRegionAvail().y, false);

        // ---- Tip of the day ----
        if (nTips > 0) {
            const TipEntry& t = m.tips[(std::size_t)st.daily];
            UI::BeginCard("##tipday");
            const UI::Theme& th = UI::Style();
            char line[200];
            std::snprintf(line, sizeof(line), "Tip of the day  \xC2\xB7  %s", catName(t.cat));
            UI::TextColored((unsigned char)(th.accent[0] * 0.6f + 102), (unsigned char)(th.accent[1] * 0.6f + 102),
                            (unsigned char)(th.accent[2] * 0.6f + 102), line);
            UI::PushTextScale(1.12f);
            UI::Text(t.title.c_str());
            UI::PopTextScale();
            UI::TextWrapped(t.body.c_str());
            if (UI::SmallButton("Next tip")) st.daily = (st.daily + 1) % nTips;
            res.nextTipButton = UI::GetItemRect();
            UI::EndCard();
            res.dailyCard = UI::GetItemRect();
        }
        UI::Spacing(6.0f);

        // ---- filter: category pills and search ----
        for (int c = 0; c < nCats; ++c) {
            if (c) UI::SameLine(6.0f);
            const bool on = st.cat == c;
            if (on) { const UI::Theme& th = UI::Style(); UI::PushStyleColor(UI::Col_Bg, th.accent[0], th.accent[1], th.accent[2], 140); }
            UI::PushID(c);
            if (UI::Button(m.cats[(std::size_t)c].c_str())) st.cat = c;
            UI::PopID();
            if (on) UI::PopStyleColor();
            res.categoryButtons.push_back(UI::GetItemRect());
        }
        UI::SameLine(16.0f);
        UI::SetNextItemWidth(std::min(260.0f, std::max(60.0f, UI::GetContentRegionAvail().x)));
        if (st.focusSearch) { UI::SetKeyboardFocusHere(); st.focusSearch = false; }
        UI::InputTextWithHint("##tipq", "Search tips", st.query, (int)sizeof(st.query));
        res.searchField = UI::GetItemRect();
        UI::Spacing(4.0f);

        // ---- the tips that pass the filter ----
        const std::string q = st.query;
        for (int i = 0; i < nTips; ++i)
            if (TipMatches(m.tips[(std::size_t)i], st.cat, q)) res.shown.push_back(i);
        if (res.shown.empty()) {
            char msg[160];
            std::snprintf(msg, sizeof(msg), "No tips match \"%s\".", st.query);
            UI::TextDisabled(msg);
            res.noMatch = true;
        }
        res.columns = UI::GetContentRegionAvail().x > 760.0f ? 2 : 1;
        if (!res.shown.empty() && UI::BeginTable("##tips", res.columns)) {
            for (int idx : res.shown) {
                const TipEntry& t = m.tips[(std::size_t)idx];
                UI::TableNextColumn();
                UI::PushID(idx);
                UI::BeginCard("##tip");
                UI::TextDisabled(catName(t.cat));
                UI::Text(t.title.c_str());
                UI::TextWrapped(t.body.c_str());
                UI::EndCard();
                res.cards.push_back(UI::GetItemRect());
                UI::PopID();
            }
            UI::EndTable();
        }
        UI::EndChild();
    }
    UI::End();
    return res;
}

// ---- Crash Log -------------------------------------------------------------------------------------------------------------

namespace {

void SplitLines(const std::string& text, std::vector<std::pair<std::size_t, std::size_t>>& out) {
    out.clear();
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            std::size_t len = i - start;
            if (len && text[start + len - 1] == '\r') --len;
            if (i < text.size() || len) out.emplace_back(start, len);
            start = i + 1;
        }
    }
}

} // namespace

CrashLogResult DrawCrashLog(const CrashLogModel& m, CrashLogState& st, const CrashLogHost& host,
                            float x, float y, float w, float h) {
    CrashLogResult res;
    if (!st.body) {
        st.body = std::make_shared<UI::CodeEdit>();
        UI::MakeTextBox(*st.body);
        st.body->SetReadOnly(true);
    }
    UI::CodeEdit& body = *st.body;
    // Ctrl+C copies the selection through the host's clipboard (the same call as Copy All); nothing is ever pasted
    body.SetClipboardFunctions([&host](const std::string& t) { if (host.copyAll) host.copyAll(t); }, [] { return std::string(); });
    if (st.seenRevision != m.revision) {
        std::vector<std::pair<std::size_t, std::size_t>> lines;
        SplitLines(m.text, lines);
        st.lineCount = (int)lines.size();
        body.SetText(m.text);
        body.SetCaret(UI::CodePos{body.LineCount() - 1, body.LineLength(body.LineCount() - 1)});   // the newest report is at the bottom
        st.revealFrames = 3;
        st.seenRevision = m.revision;
    }
    if (st.revealFrames > 0) { --st.revealFrames; body.ScrollToCaret(); }
    res.empty = m.text.empty();
    res.lineCount = st.lineCount;

    UI::BareNextWindow();
    if (UI::Begin("Crash Log##okayui", x, y, w, h)) {
        if (UI::Button("Refresh") && host.refresh) host.refresh();
        res.refreshButton = UI::GetItemRect();
        UI::SameLine();
        UI::BeginDisabled(m.text.empty());
        if (UI::Button("Copy All") && host.copyAll) host.copyAll(m.text);
        res.copyAllButton = UI::GetItemRect();
        UI::EndDisabled();
        if (UI::IsItemHovered() && !m.text.empty()) UI::SetTooltip("%s", "Copy every report to the clipboard, then paste it to support.");
        UI::SameLine();
        if (UI::Button("Open Folder") && host.openFolder) host.openFolder();
        res.openFolderButton = UI::GetItemRect();
        UI::SameLine();
        if (UI::Button("Clear")) { st.confirming = true; UI::OpenPopup("Clear crash reports?"); }
        res.clearButton = UI::GetItemRect();
        if (UI::IsItemHovered()) UI::SetTooltip("%s", "Delete the saved crash reports.");
        if (UI::BeginPopupModal("Clear crash reports?")) {
            UI::Text("Delete all saved crash reports from disk?");
            UI::TextDisabled("This can't be undone.");
            UI::Spacing(4.0f);
            UI::PushStyleColor(UI::Col_Bg, 166, 56, 56);
            UI::PushStyleColor(UI::Col_BgHover, 204, 71, 71);
            const bool doClear = UI::Button("Delete");
            res.confirmDelete = UI::GetItemRect();
            UI::PopStyleColor(2);
            if (doClear) { if (host.clear) host.clear(); UI::CloseCurrentPopup(); }
            UI::SameLine();
            if (UI::Button("Cancel")) UI::CloseCurrentPopup();
            res.confirmCancel = UI::GetItemRect();
            UI::EndPopup();
        }
        st.confirming = UI::IsPopupOpen("Clear crash reports?");
        UI::SameLine();
        UI::TextDisabled(m.path.c_str());
        UI::Separator();

        if (m.text.empty()) {
            UI::TextColored(kGreen[0], kGreen[1], kGreen[2], "No crash reports \xE2\x80\x94 nice.");
            UI::TextDisabled("If the editor ever crashes, a report is written next to the exe and");
            UI::TextDisabled("appears here. Use Copy All to send it to support.");
        } else {
            char info[96];
            std::snprintf(info, sizeof(info), "Newest report is at the bottom. \xE2\x80\xA2 %d bytes", (int)m.text.size());
            UI::TextDisabled(info);
            body.SetPalette(UI::TextBoxPalette());
            const float bw = std::max(UI::GetContentRegionAvail().x, 32.0f), bh = std::max(UI::GetContentRegionAvail().y, 24.0f);
            body.Draw("##crashbody", bw, bh);
            res.body = UI::GetItemRect();
            // what is on screen: only these lines are laid out
            res.firstDrawn = std::min(body.FirstVisibleLine(), std::max(st.lineCount - 1, 0));
            res.lastDrawn = std::min(body.FirstVisibleLine() + body.VisibleLineCount(), st.lineCount - 1);   // (and the part-shown line)
            res.linesDrawn = std::max(res.lastDrawn - res.firstDrawn + 1, 0);
        }
    }
    UI::End();
    return res;
}

// ---- Audio Mixer -----------------------------------------------------------------------------------------------------------

float MeterDb(float v) { return v > 1e-5f ? 20.0f * std::log10(v) : -60.0f; }
float MeterFill(float v) {
    if (!(v == v)) return 0.0f;
    return std::min(std::max((MeterDb(v) + 60.0f) / 60.0f, 0.0f), 1.0f);
}

AudioMixerResult DrawAudioMixer(AudioMixerModel& m, float x, float y, float w, float h) {
    AudioMixerResult res;
    UI::BareNextWindow();
    if (UI::Begin("Audio Mixer##okayui", x, y, w, h)) {
        UI::BeginChild("##mixer", 0.0f, UI::GetContentRegionAvail().y, false);   // a short window scrolls instead of cutting the bottom off
        auto meter = [&](const char* name, float v, UI::Rect& out) {
            const float width = std::max(UI::GetContentRegionAvail().x, 8.0f);
            const UI::Vec2 p = UI::GetCursorScreenPos();
            const UI::DrawList& dl = UI::GetWindowDrawList();
            dl.AddRectFilled(p, UI::Vec2{p.x + width, p.y + 6.0f}, UI::RGBA(26, 28, 34), 2.0f);
            const float t = MeterFill(v);
            const UI::Color32 fill = v >= 1.0f ? UI::RGBA(217, 77, 77) : UI::RGBA(89, 179, 115);
            if (t > 0.0f) dl.AddRectFilled(p, UI::Vec2{p.x + width * t, p.y + 6.0f}, fill, 2.0f);
            UI::Dummy(width, 6.0f);
            out = UI::GetItemRect();
            if (UI::IsItemHovered() && UI::BeginTooltip()) {   // a tooltip scope: the meter name, then the level in dB on its own line
                UI::Text(name);
                char tip[64]; std::snprintf(tip, sizeof(tip), "%.1f dB", MeterDb(v));
                UI::TextDisabled(tip);
                UI::EndTooltip();
            }
        };
        UI::TextDisabled("Output");
        meter("Left", m.peakLeft, res.leftMeter);
        meter("Right", m.peakRight, res.rightMeter);
        UI::Spacing(4.0f);

        // Group name, volume, mute: three columns, the middle one takes the rest.
        const float nameW = 90.0f, muteW = 60.0f;
        auto row = [&](const char* name, float* vol, bool* mute, UI::Rect& sl, UI::Rect& mu, const char* id) {
            UI::PushID(id);
            UI::Text(name);
            UI::SameLine();
            UI::SetCursorPosX(nameW);
            UI::SetNextItemWidth(std::max(60.0f, UI::GetContentRegionAvail().x - muteW));
            UI::PushID("v");   // the two widgets have no visible label, so each gets its own id scope
            if (vol && UI::SliderFloat("", vol, 0.0f, 1.0f)) res.changed = true;
            sl = UI::GetItemRect();
            UI::PopID();
            UI::SameLine();
            UI::PushID("m");
            if (mute && UI::Checkbox("", mute)) res.changed = true;
            mu = UI::GetItemRect();
            UI::PopID();
            UI::PopID();
        };
        row("Master", m.masterVolume, m.masterMuted, res.masterSlider, res.masterMute, "master");
        for (std::size_t i = 0; i < m.groupNames.size(); ++i) {
            UI::Rect sl, mu;
            row(m.groupNames[i].c_str(), i < m.groupVolume.size() ? m.groupVolume[i] : nullptr,
                i < m.groupMuted.size() ? m.groupMuted[i] : nullptr, sl, mu, m.groupNames[i].c_str());
            res.groupSliders.push_back(sl);
            res.groupMutes.push_back(mu);
        }
        UI::Spacing(4.0f);
        if (m.duckMusic) {
            if (UI::Checkbox("Duck music under voice", m.duckMusic)) res.changed = true;
            res.duckToggle = UI::GetItemRect();
            if (UI::IsItemHovered()) UI::SetTooltip("%s", "While a Voice-group source plays, the Music group dips to the duck level.");
            if (*m.duckMusic) {
                if (m.duckLevel && UI::SliderFloat("Duck level", m.duckLevel, 0.0f, 1.0f)) res.changed = true;
                if (m.duckAttack && UI::SliderFloat("Attack (s)", m.duckAttack, 0.0f, 2.0f)) res.changed = true;
                if (m.duckRelease && UI::SliderFloat("Release (s)", m.duckRelease, 0.0f, 4.0f)) res.changed = true;
                char buf[48];
                std::snprintf(buf, sizeof(buf), "Music now at %.0f%%", m.duckGain * 100.0f);
                UI::TextDisabled(buf);
            }
        }
        UI::Spacing(4.0f);
        if (UI::Button("Reset")) {
            if (m.masterVolume) *m.masterVolume = 1.0f;
            if (m.masterMuted) *m.masterMuted = false;
            for (float* v : m.groupVolume) if (v) *v = 1.0f;
            for (bool* b : m.groupMuted) if (b) *b = false;
            res.reset = res.changed = true;
        }
        res.resetButton = UI::GetItemRect();
        UI::SameLine();
        UI::TextDisabled("Editor only; players set their own in the pause menu.");
        UI::EndChild();
    }
    UI::End();
    return res;
}

} // namespace okayuiscreens
