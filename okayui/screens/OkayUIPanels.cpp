#include "OkayUIPanels.hpp"
#include "okay/UI/OkayUIImCompat.hpp"
#include "okay/UI/OkayUICompat.hpp"

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
    if (st.seenRevision != m.revision) {
        SplitLines(m.text, st.lines);
        st.seenRevision = m.revision;
        st.scrollToEnd = true;
        st.settle = 0;
    }
    res.empty = m.text.empty();
    res.lineCount = (int)st.lines.size();

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
            UI::BeginChild("##crashbody", 0.0f, UI::GetContentRegionAvail().y, true);
            res.body = UI::GetItemRect();
            // One row per line, only the visible ones laid out; the rows above and below are one spacer each.
            // The row pitch is measured from the rows themselves (it depends on the look and the text scale) and kept for the next
            // frame; until then a guess is used. A spacer of n rows is n pitches minus the gap the spacer item adds below itself.
            const float rowH = st.pitch > 0.0f ? st.pitch : UI::GetTextLineHeightWithSpacing();
            const float gap = st.pitch > 0.0f ? st.gap : 0.0f;
            const int n = (int)st.lines.size();
            const float viewH = UI::GetContentRegionAvail().y;
            int first = (int)(UI::GetScrollY() / rowH) - 1;
            int last = first + (int)(viewH / rowH) + 3;
            first = std::max(first, 0);
            last = std::min(last, n);
            if (first > 0) UI::Dummy(1.0f, rowH * (float)first - gap);
            std::string row;
            for (int i = first; i < last; ++i) {
                const auto& ln = st.lines[(std::size_t)i];
                row.assign(m.text, ln.first, ln.second);
                for (char& c : row) if (c == '\t') c = ' ';
                const float y0 = UI::GetCursorScreenPos().y;
                if (row.empty()) UI::Dummy(1.0f, std::max(rowH - gap, 1.0f)); else UI::Text(row.c_str());
                if (i == first && !row.empty()) {
                    const float pitch = UI::GetCursorScreenPos().y - y0;
                    if (pitch > 1.0f) { st.pitch = pitch; st.gap = std::max(pitch - UI::GetItemRect().h, 0.0f); }
                }
                if (res.linesDrawn == 0) res.firstDrawn = i;
                res.lastDrawn = i;
                ++res.linesDrawn;
            }
            if (last < n) UI::Dummy(1.0f, rowH * (float)(n - last) - gap);
            if (st.scrollToEnd) {   // the extent is measured a frame late: keep asking until the end is reached (or a few frames pass)
                UI::SetScrollY(1e9f);
                const bool atEnd = UI::GetScrollMaxY() > 0.0f && UI::GetScrollY() >= UI::GetScrollMaxY() - 1.0f;
                if ((atEnd && st.settle >= 2) || ++st.settle > 12) st.scrollToEnd = false;
                else if (atEnd) ++st.settle;
            }
            UI::EndChild();
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
            if (UI::IsItemHovered()) { char tip[64]; std::snprintf(tip, sizeof(tip), "%s: %.1f dB", name, MeterDb(v)); UI::SetTooltip("%s", tip); }
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

// ---- Stats -------------------------------------------------------------------------------------------------------------------
// Written from what the editor's Stats window shows (its fields and colours), using OkayUI's own plot, table and check boxes.

int FpsBand(float f) { return !std::isfinite(f) ? 2 : (f >= 55.0f ? 0 : (f >= 30.0f ? 1 : 2)); }

void HistoryStats(const float* h, int n, float* mn, float* mx, float* avg) {
    float lo = 1e9f, hi = 0.0f, sum = 0.0f; int cnt = 0;
    for (int i = 0; h && i < n; ++i) if (std::isfinite(h[i]) && h[i] > 0.0f) { lo = std::min(lo, h[i]); hi = std::max(hi, h[i]); sum += h[i]; ++cnt; }
    if (mn) *mn = cnt ? lo : 0.0f;
    if (mx) *mx = hi;
    if (avg) *avg = cnt ? sum / (float)cnt : 0.0f;
}

float PlotScale(float maxValue) {
    if (!std::isfinite(maxValue) || maxValue < 0.0f) maxValue = 0.0f;
    return maxValue * 1.2f < 75.0f ? 75.0f : maxValue * 1.2f;
}

std::string FormatCount(int v) {
    char b[32];
    if (v >= 1000000) std::snprintf(b, sizeof b, "%.1fM", v / 1e6);
    else if (v >= 10000) std::snprintf(b, sizeof b, "%.1fk", v / 1e3);
    else std::snprintf(b, sizeof b, "%d", v);
    return b;
}

StatsResult DrawStats(const StatsModel& m, float x, float y, float w, float h) {
    StatsResult res;
    UI::BareNextWindow();
    if (UI::Begin("Stats##okayui", x, y, w, h)) {
        UI::BeginChild("##stats", 0.0f, UI::GetContentRegionAvail().y, false);
        res.fpsBand = FpsBand(m.framerate);
        float fmin = 0, fmax = 0, favg = 0;
        HistoryStats(m.history, m.historyCount, &fmin, &fmax, &favg);
        res.plotMax = PlotScale(fmax);
        {   // the big readout, the frame time, and the average and minimum on the right
            static const unsigned char good[3] = {115, 217, 140}, mid[3] = {242, 199, 89}, poor[3] = {245, 115, 107};
            const unsigned char* c = res.fpsBand == 0 ? good : (res.fpsBand == 1 ? mid : poor);
            const UI::Vec2 p0 = UI::GetCursorScreenPos();
            UI::PushTextScale(2.0f);
            char fps[24]; std::snprintf(fps, sizeof fps, "%.0f", std::isfinite(m.framerate) ? m.framerate : 0.0f);
            UI::TextColored(c[0], c[1], c[2], fps);
            UI::PopTextScale();
            const UI::Vec2 p1 = UI::GetItemRectMax();
            res.fpsReadout = UI::Rect{p0.x, p0.y, p1.x - p0.x, p1.y - p0.y};
            UI::SameLine();
            char ms[48]; std::snprintf(ms, sizeof ms, "FPS   %.2f ms", m.framerate > 0.0f && std::isfinite(m.framerate) ? 1000.0f / m.framerate : 0.0f);
            UI::TextDisabled(ms);
            char mm[48]; std::snprintf(mm, sizeof mm, "avg %.0f  min %.0f", favg, fmin);
            UI::SameLine();
            const float right = UI::GetContentRegionMax().x - UI::CalcTextWidth(mm);
            if (UI::GetCursorPosX() < right) UI::SetCursorPosX(right);
            UI::TextDisabled(mm);
        }
        {   // the plot of the history, auto-scaled, with a guide at 60 fps
            UI::PlotOptions po; po.valuesOffset = m.historyCount > 0 ? ((m.historyOffset % m.historyCount) + m.historyCount) % m.historyCount : 0;
            po.scaleMin = 0.0f; po.scaleMax = res.plotMax; po.size = UI::Vec2{-1.0f, 48.0f};
            if (m.history && m.historyCount > 0) UI::PlotLines("##fps", m.history, m.historyCount, po);
            else UI::Dummy(UI::GetContentRegionAvail().x, 48.0f);
            res.plot = UI::GetItemRect();
            if (res.plotMax > 60.0f) {
                const float gy = res.plot.y + res.plot.h - (60.0f / res.plotMax) * res.plot.h;
                UI::GetWindowDrawList().AddLine(UI::Vec2{res.plot.x, gy}, UI::Vec2{res.plot.x + res.plot.w, gy}, UI::RGBA(110, 220, 130, 110), 1.0f);
            }
        }
        UI::Spacing(4.0f);
        UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{8.0f, 0.0f});   // the grid is a dense list: rows touch, as in the Dear ImGui window
        if (UI::BeginTable("##statgrid", 4, UI::Table_None)) {
            UI::TableSetupColumn("n1", UI::Col_WidthStretch, 1.0f); UI::TableSetupColumn("v1", UI::Col_WidthStretch, 0.8f);
            UI::TableSetupColumn("n2", UI::Col_WidthStretch, 1.0f); UI::TableSetupColumn("v2", UI::Col_WidthStretch, 0.8f);
            for (std::size_t i = 0; i < m.rows.size(); ++i) {
                if (i % 2 == 0) UI::TableNextRow();
                UI::TableNextColumn();
                UI::TextDisabled(m.rows[i].name.c_str());
                UI::TableNextColumn();
                const std::string num = FormatCount(m.rows[i].value);
                if (m.rows[i].value == 0) UI::TextDisabled(num.c_str()); else UI::Text(num.c_str());
                ++res.cellsDrawn;
            }
            UI::EndTable();
        }
        UI::PopStyleVar();
        UI::Spacing(4.0f);
        auto toggle = [&](const char* label, bool* v, UI::Rect& out, const char* tip) {
            if (!v) return false;
            if (UI::Checkbox(label, v)) res.changed = true;
            out = UI::GetItemRect();
            if (tip && UI::IsItemHovered(UI::HoveredFlags_ForTooltip) && UI::BeginTooltip()) {
                std::string line;   // Text draws one line, so the help is shown a line at a time
                for (const char* c = tip;; ++c) {
                    if (*c == '\n' || *c == 0) { UI::Text(line.c_str()); line.clear(); if (*c == 0) break; }
                    else line += *c;
                }
                UI::EndTooltip();
            }
            return true;
        };
        if (toggle("Frustum culling", m.frustumCulling, res.frustum, nullptr)) UI::SameLine();
        toggle("Occlusion", m.occlusionCulling, res.occlusion, "Skip meshes hidden behind an Occluder. Off, or with no Occluder\nin the scene, nothing extra is hidden.");
        if (toggle("Static batching", m.staticBatching, res.staticBatch, nullptr)) UI::SameLine();
        toggle("Dynamic batching", m.dynamicBatching, res.dynamicBatch, "Skip meshes the camera (or the sun's shadow map) can't see.\nTurn off only to check whether culling hides something it shouldn't.");
        UI::SameLine();
        char sk[48]; std::snprintf(sk, sizeof sk, "| skinning: %s", m.skinning.c_str());
        UI::TextDisabled(sk);
        UI::Spacing(4.0f);
        UI::TextDisabled("Sky, lighting, post FX and debug view moved to the Lighting tab.");
        UI::EndChild();
    }
    UI::End();
    return res;
}

} // namespace okayuiscreens
