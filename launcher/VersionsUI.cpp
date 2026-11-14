// The launcher's Versions page. The rules are in VersionsManager.cpp and the download in VersionsNet.cpp; this file draws the
// page and runs its two background jobs on SDL threads (the launcher does not use std::thread / std::mutex: see main.cpp).
#include "VersionsUI.hpp"

#include <SDL.h>
#include "okay/Platform/Platform.hpp"   // okay::platform: timers (and the SDL seam)

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>

#include "VersionsNet.hpp"

namespace okayversionsui {
namespace {

using namespace okayversions;
namespace fs = std::filesystem;

// ---- background jobs --------------------------------------------------------------------------------------------------------

struct IndexJob {
    SDL_Thread* th = nullptr;
    std::atomic<bool> done{false};
    Sources sources;
    FetchTextFn fetch;
    Index result;
};
int IndexWorker(void* p) {
    IndexJob* j = static_cast<IndexJob*>(p);
    j->result = LoadIndex(j->sources, j->fetch);
    j->done = true;
    return 0;
}

struct InstallJob {
    SDL_Thread* th = nullptr;
    std::atomic<bool> done{false}, cancel{false};
    std::atomic<std::uint64_t> fileBytes{0}, fileTotal{0}, doneBytes{0}, totalBytes{0};
    std::atomic<int> fileIndex{0}, fileCount{0};
    SDL_mutex* mu = nullptr;
    std::string file;                                   // guarded by mu
    std::string version;
    fs::path launcherDir;
    InstallPlan plan;
    InstallOptions options;
    Result result;
    ~InstallJob() { if (mu) SDL_DestroyMutex(mu); }
};
int InstallWorker(void* p) {
    InstallJob* j = static_cast<InstallJob*>(p);
    InstallPlan plan = j->plan;
    j->totalBytes = plan.totalBytes();
    IO io;
    io.download = &net::DownloadFile;
    io.cancelled = [j] { return j->cancel.load(); };
    io.progress = [j](const Progress& pr) {
        j->fileBytes = pr.fileBytes;
        j->fileTotal = pr.fileTotal;
        j->doneBytes = pr.doneBytes;
        j->totalBytes = pr.totalBytes;
        j->fileIndex = pr.fileIndex;
        j->fileCount = pr.fileCount;
        SDL_LockMutex(j->mu);
        j->file = pr.file;
        SDL_UnlockMutex(j->mu);
    };
    j->result = Install(j->launcherDir, plan, j->options, io);
    j->done = true;
    return 0;
}

// ---- the page's state (main thread only, except what the jobs own) ---------------------------------------------------------------

struct State {
    std::unique_ptr<IndexJob> indexJob;
    Index index;
    bool haveIndex = false;
    Uint32 indexAt = 0;
    std::string requested;                              // --versions <version> (normalized)
    std::string highlight;                              // the row to point at
    bool pageRequested = false, scrollToHighlight = false;
    std::vector<Installed> installed;
    std::vector<std::string> installedVersions;
    Uint32 installedAt = 0;
    bool installedDirty = true;
    std::uint64_t usage = 0;
    std::unique_ptr<InstallJob> job;
    std::map<std::string, std::pair<std::string, int>> notes;   // per version: the last message and its kind (2 = error)
    std::string removeAsk;                              // a Remove confirmation is open for this version
    bool showAllAvailable = false;
    bool staleCleaned = false;
};
State& S() {
    static State s;
    return s;
}

// ---- small drawing helpers ------------------------------------------------------------------------------------------------------

const ImU32 kGreen = IM_COL32(110, 205, 140, 255), kRed = IM_COL32(240, 125, 120, 255), kAmber = IM_COL32(240, 200, 100, 255);

void CardBegin(const Host& h, const char* id, const char* title) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, h.cardBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 14));
    ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    if (title) {
        ImGui::SetWindowFontScale(0.80f);
        ImGui::TextDisabled("%s", title);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Dummy(ImVec2(0, 4));
    }
}
void CardEnd() {
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 10));
}

ImU32 Tint(const ImVec4& c, float a) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, a)); }

/// A small rounded label at `p`; returns its width.
float DrawPill(ImDrawList* dl, ImVec2 p, const char* text, ImU32 bg, ImU32 fg) {
    const float fs = ImGui::GetFontSize() * 0.86f;
    const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    const float w = ts.x + 14.0f, hgt = ts.y + 5.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + hgt), bg, hgt * 0.5f);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + 7.0f, p.y + 2.5f), fg, text);
    return w;
}
float TextWidth(const char* text, float scale = 1.0f) { return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * scale, FLT_MAX, 0.0f, text).x; }
void DrawTextAt(ImDrawList* dl, ImVec2 p, const char* text, ImU32 col, float scale = 1.0f) {
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, p, col, text);
}
/// The text cut to `maxW` with an ellipsis.
std::string Fit(const std::string& s, float maxW, float scale = 1.0f) {
    if (TextWidth(s.c_str(), scale) <= maxW) return s;
    std::string out = s;
    while (!out.empty()) {
        out.pop_back();
        while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80) out.pop_back();
        if (TextWidth((out + "\xE2\x80\xA6").c_str(), scale) <= maxW) break;
    }
    return out + "\xE2\x80\xA6";
}

bool AccentButton(const Host& h, const char* label, const ImVec2& size) {
    auto lift = [](float x) { return x + (1.0f - x) * 0.20f; };
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(h.accent.x, h.accent.y, h.accent.z, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(lift(h.accent.x), lift(h.accent.y), lift(h.accent.z), 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, h.accentDim);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool hit = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return hit;
}
bool DangerButton(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.26f, 0.26f, 1.0f));
    const bool hit = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    return hit;
}

std::string V(const std::string& version) { return "v" + version; }

void Toast(const Host& h, const std::string& m, int kind) { if (h.toast) h.toast(m, kind); }

// ---- state changes -----------------------------------------------------------------------------------------------------------------

void RefreshInstalled(const Host& h) {
    State& st = S();
    st.installed = ListInstalled(h.launcherDir);
    st.installedVersions.clear();
    for (const Installed& i : st.installed) st.installedVersions.push_back(i.version);
    st.usage = DiskUsage(h.launcherDir);
    st.installedDirty = false;
    st.installedAt = okay::platform::TicksMs32();
}

void ResolveHighlight() {
    State& st = S();
    st.highlight.clear();
    if (st.requested.empty()) return;
    if (st.haveIndex && st.index.ok) st.highlight = PickHighlight(st.index, st.requested);
    for (const Installed& i : st.installed)                                      // an installed one of that line counts too
        if (st.highlight.empty() && okayver::SameLine(i.version, st.requested)) st.highlight = i.version;
    st.scrollToHighlight = !st.highlight.empty();
}

void StartIndex(const Host& h) {
    State& st = S();
    if (st.indexJob) return;
    st.indexJob = std::make_unique<IndexJob>();
    st.indexJob->sources = h.sources;
    st.indexJob->fetch = h.fetchText;
    st.indexJob->th = SDL_CreateThread(IndexWorker, "okay-versions-index", st.indexJob.get());
    if (!st.indexJob->th) st.indexJob.reset();
}

void StartJob(const Host& h, const InstallPlan& plan) {
    State& st = S();
    if (st.job) return;
    auto j = std::make_unique<InstallJob>();
    j->mu = SDL_CreateMutex();
    j->version = plan.version;
    j->launcherDir = h.launcherDir;
    j->plan = plan;
    j->totalBytes = plan.totalBytes();
    j->th = SDL_CreateThread(InstallWorker, "okay-versions-install", j.get());
    if (!j->th) { Toast(h, "Couldn't start the download.", 2); return; }
    st.notes.erase(plan.version);
    st.job = std::move(j);
}

void BeginInstall(const Host& h, const std::string& version) {
    State& st = S();
    if (st.job || !st.haveIndex) return;
    const InstallPlan plan = PlanInstall(st.index, version, h.sources.bucketBase, FindCurrentInstall(h.launcherDir).sdl);
    if (!plan.ok) {
        st.notes[version] = {plan.error, 2};
        Toast(h, plan.error, 2);
        return;
    }
    StartJob(h, plan);   // (a version without checksums never gets a plan: PlanInstall refuses it with the reason)
}

RemoveGuards MakeGuards(const Host& h) {
    RemoveGuards g;
    g.running = h.currentVersion;
    g.launcher = h.launcherVersion;
    const std::string def = GlobalSettings().defaultEditor;
    if (!def.empty()) g.keep.push_back({def, "it is your default editor. Choose another default editor first."});
    if (h.editorRunning && !h.runningEditorVersion.empty()) {
        const std::string rv = okayver::NormalizeVersion(h.runningEditorVersion);
        g.inUse = [rv](const std::string& v) { return v == rv; };
    }
    return g;
}

// ---- the cards -----------------------------------------------------------------------------------------------------------------------

void DrawRequestBanner(const Host& h) {
    State& st = S();
    if (st.requested.empty()) return;
    const std::string line = okayver::LineLabel(st.requested);
    std::string text, installedMatch;
    ImU32 col = kAmber;
    for (const Installed& i : st.installed) if (okayver::SameLine(i.version, st.requested)) { installedMatch = i.version; break; }
    if (!installedMatch.empty()) { text = line + " is already installed (" + V(installedMatch) + "). Projects of that version can be opened with it from their Open with menu."; col = kGreen; }
    else if (!st.haveIndex || st.indexJob) text = "Looking for " + line + " in the version list\xE2\x80\xA6";
    else if (!st.highlight.empty()) text = "The editor needs " + line + " to open that project with its own version. Install " + V(st.highlight) + " below.";
    else if (!st.index.ok) text = line + " isn't installed, and the version list can't be read right now, so it can't be installed from here.";
    else text = line + " isn't installed and isn't in the version list, so it can't be installed from here.";
    const ImVec4 base = col == kGreen ? ImVec4(0.43f, 0.80f, 0.55f, 1.0f) : ImVec4(0.94f, 0.78f, 0.39f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(base.x, base.y, base.z, 0.14f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 12));
    ImGui::BeginChild("##verreq", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - 90.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SameLine(ImGui::GetCursorStartPos().x + w - 76.0f);
    if (ImGui::SmallButton("Dismiss")) { st.requested.clear(); st.highlight.clear(); }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 10));
    (void)h;
}

void DrawThisVersion(const Host& h) {
    State& st = S();
    CardBegin(h, "##verthis", "THIS VERSION");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float W = ImGui::GetContentRegionAvail().x;
    const float rowH = 54.0f;
    const std::string cur = okayver::NormalizeVersion(h.currentVersion);
    const std::string title = cur.empty() ? "You're running a development build" : "You're running " + V(cur);
    DrawTextAt(dl, ImVec2(p0.x, p0.y + 2), title.c_str(), ImGui::GetColorU32(ImGuiCol_Text), 1.18f);
    float x = p0.x + TextWidth(title.c_str(), 1.18f) + 12.0f;
    if (st.haveIndex && st.index.ok && !cur.empty()) {
        if (cur == st.index.latest) x += DrawPill(dl, ImVec2(x, p0.y + 5), "Latest", Tint(h.accent, 0.32f), ImGui::GetColorU32(ImGuiCol_Text)) + 6.0f;
        if (okayver::SameLine(cur, st.index.lts)) x += DrawPill(dl, ImVec2(x, p0.y + 5), "LTS", IM_COL32(70, 170, 120, 90), ImGui::GetColorU32(ImGuiCol_Text)) + 6.0f;
    }
    std::string sub;
    const std::string launcher = okayver::NormalizeVersion(h.launcherVersion);
    if (launcher.empty() || cur.empty() || launcher == cur) sub = "The launcher, the editor and the player are all this version.";
    else sub = "The editor and player are " + V(cur) + "; this launcher window is still " + V(launcher) + " until you restart it.";
    DrawTextAt(dl, ImVec2(p0.x, p0.y + 30), Fit(sub, W - 330.0f).c_str(), ImGui::GetColorU32(ImGuiCol_TextDisabled));

    const bool busy = st.indexJob != nullptr;
    ImGui::SetCursorScreenPos(ImVec2(p0.x + W - 306.0f, p0.y + 8));
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(busy ? "Checking\xE2\x80\xA6" : "Check again", ImVec2(140, 30))) StartIndex(h);
    ImGui::EndDisabled();
    ImGui::SameLine(0, 8);
    if (ImGui::Button("Open install folder", ImVec2(158, 30))) {
        std::error_code ec;
        fs::create_directories(okayver::VersionsDir(h.launcherDir), ec);
        if (h.openFolder) h.openFolder(okayver::VersionsDir(h.launcherDir).string());
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + rowH));
    ImGui::Dummy(ImVec2(W, 0));

    // How complete the version list is.
    if (st.indexJob && !st.haveIndex) ImGui::TextDisabled("Looking up the version list\xE2\x80\xA6");
    else if (st.haveIndex && !st.index.ok) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kRed), "%s", st.index.error.c_str());
    else if (st.haveIndex && !st.index.note.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAmber), "%s", st.index.note.c_str());
        ImGui::PopTextWrapPos();
    }
    CardEnd();
}

void DrawDefaultEditor(const Host& h, float width) {
    State& st = S();
    Settings& set = GlobalSettings();
    const std::string cur = okayver::NormalizeVersion(h.currentVersion);
    const std::string thisLabel = "This version" + (cur.empty() ? std::string() : " (" + V(cur) + ")");
    const std::string preview = set.defaultEditor.empty() ? thisLabel : V(set.defaultEditor);
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##defed", preview.c_str())) {
        if (ImGui::Selectable(thisLabel.c_str(), set.defaultEditor.empty())) { set.defaultEditor.clear(); if (h.savePrefs) h.savePrefs(); }
        for (const Installed& i : st.installed) {
            std::string label = V(i.version);
            if (st.haveIndex && okayver::SameLine(i.version, st.index.lts)) label += "   long-term support";
            if (ImGui::Selectable(label.c_str(), set.defaultEditor == i.version)) { set.defaultEditor = i.version; if (h.savePrefs) h.savePrefs(); }
        }
        ImGui::EndCombo();
    }
}

void DrawChannel(const Host& h) {
    State& st = S();
    Settings& set = GlobalSettings();
    CardBegin(h, "##verchan", "UPDATE CHANNEL");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Follow");
    ImGui::SameLine(0, 10);
    DrawChannelCombo(h, 260.0f);
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushTextWrapPos(0.0f);
    if (set.channel == Channel::Latest) {
        ImGui::TextUnformatted("Latest gets every new release as soon as it is out: new features and fixes. Long-term support (LTS) is a version that only gets "
                               "fixes, for projects that should keep working exactly as they do now.");
    } else {
        const std::string head = st.haveIndex && st.index.ok ? st.index.lts : std::string();
        ImGui::TextUnformatted((head.empty() ? std::string("The long-term-support version only gets fixes.") : LtsSentence(head)).c_str());
        ImGui::TextDisabled("The launcher itself is never downgraded. When it is newer than the long-term-support line, the long-term-support editor is "
                            "installed next to it (in Versions) and you can use it as your default editor: it opens projects that have no version yet, "
                            "or whenever you pick it from a project's Open with menu.");
    }
    ImGui::PopTextWrapPos();

    if (set.channel == Channel::Lts && st.haveIndex) {
        ImGui::Dummy(ImVec2(0, 8));
        const ChannelDecision d = DecideChannel(Channel::Lts, h.currentVersion, st.index.latest, st.index.lts, st.installedVersions);
        const bool busy = st.job != nullptr;
        ImGui::AlignTextToFramePadding();
        switch (d.kind) {
            case ChannelDecision::InstallSideBySide:
            case ChannelDecision::UpdateSideBySide: {
                const bool first = d.kind == ChannelDecision::InstallSideBySide;
                const std::string sentence = first ? "Install " + V(d.version) + " next to this version." : V(d.version) + " is out. Update the copy next to this version.";
                const std::string label = (first ? "Install " : "Update to ") + V(d.version);
                ImGui::TextUnformatted(sentence.c_str());
                ImGui::SameLine(0, 12);
                ImGui::BeginDisabled(busy);
                if (AccentButton(h, label.c_str(), ImVec2(0, 0))) BeginInstall(h, d.version);
                ImGui::EndDisabled();
                break;
            }
            case ChannelDecision::UpdateInPlace:
                ImGui::TextUnformatted((V(d.version) + " is out for the long-term-support line. " + d.note).c_str());
                ImGui::SameLine(0, 12);
                if (AccentButton(h, "Update the launcher", ImVec2(0, 0)) && h.startUpdate) h.startUpdate();
                break;
            case ChannelDecision::NoInfo: ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAmber), "%s", d.note.c_str()); break;
            case ChannelDecision::None: ImGui::TextDisabled("%s", d.note.c_str()); break;
        }
        const std::string have = okayver::ResolveKeepTarget(st.index.lts, st.installedVersions);
        if (!have.empty() && d.kind != ChannelDecision::UpdateInPlace) {
            ImGui::Dummy(ImVec2(0, 4));
            const bool isDefault = set.defaultEditor == have;
            if (isDefault) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kGreen), "%s is your default editor.", V(have).c_str());
                ImGui::SameLine(0, 12);
                if (ImGui::Button("Use this version's editor again")) { set.defaultEditor.clear(); if (h.savePrefs) h.savePrefs(); }
            } else if (AccentButton(h, ("Use " + okayver::LineLabel(have) + " as my default editor").c_str(), ImVec2(0, 0))) {
                set.defaultEditor = have;
                if (h.savePrefs) h.savePrefs();
                Toast(h, V(have) + " is now your default editor.", 1);
            }
        }
    }

    // The default editor, for any installed version.
    if (!st.installed.empty() || !set.defaultEditor.empty()) {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1, 1, 1, 0.06f));
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Default editor");
        ImGui::SameLine(0, 12);
        DrawDefaultEditor(h, 300.0f);
        ImGui::SameLine(0, 12);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Opens projects that have no version yet.");
    }
    CardEnd();
}

/// One row of a version list: the version, pills, a line of detail, and room on the right for `rightW` pixels of controls.
struct Row {
    ImVec2 p0;
    float width = 0, height = 58.0f;
    ImDrawList* dl = nullptr;
};
Row RowBegin(float height = 58.0f) {
    Row r;
    r.p0 = ImGui::GetCursorScreenPos();
    r.width = ImGui::GetContentRegionAvail().x;
    r.height = height;
    r.dl = ImGui::GetWindowDrawList();
    return r;
}
void RowEnd(const Row& r, bool divider = true) {
    ImGui::SetCursorScreenPos(ImVec2(r.p0.x, r.p0.y + r.height));
    ImGui::Dummy(ImVec2(r.width, 0));
    if (divider) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        r.dl->AddLine(ImVec2(p.x, p.y - 1), ImVec2(p.x + r.width, p.y - 1), IM_COL32(255, 255, 255, 14), 1.0f);
    }
}
void RowHighlight(const Host& h, const Row& r) {
    r.dl->AddRectFilled(ImVec2(r.p0.x - 8, r.p0.y + 1), ImVec2(r.p0.x + r.width + 8, r.p0.y + r.height - 2), Tint(h.accent, 0.10f), 8.0f);
    r.dl->AddRect(ImVec2(r.p0.x - 8, r.p0.y + 1), ImVec2(r.p0.x + r.width + 8, r.p0.y + r.height - 2), Tint(h.accent, 0.70f), 8.0f, 0, 1.4f);
    if (S().scrollToHighlight) { ImGui::SetScrollHereY(0.35f); S().scrollToHighlight = false; }
}

/// "v2.850.1" and its pills; returns the x after them.
float RowTitle(const Host& h, const Row& r, const std::string& version, bool latest, bool lts, bool isDefault) {
    State& st = S();
    (void)st;
    const std::string title = V(version);
    DrawTextAt(r.dl, ImVec2(r.p0.x, r.p0.y + 7), title.c_str(), ImGui::GetColorU32(ImGuiCol_Text), 1.14f);
    float x = r.p0.x + TextWidth(title.c_str(), 1.14f) + 10.0f;
    const ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);
    if (latest) x += DrawPill(r.dl, ImVec2(x, r.p0.y + 9), "Latest", Tint(h.accent, 0.32f), fg) + 6.0f;
    if (lts) x += DrawPill(r.dl, ImVec2(x, r.p0.y + 9), "LTS", IM_COL32(70, 170, 120, 90), fg) + 6.0f;
    if (isDefault) x += DrawPill(r.dl, ImVec2(x, r.p0.y + 9), "Default editor", IM_COL32(255, 255, 255, 30), fg) + 6.0f;
    return x;
}

void DrawInstalled(const Host& h) {
    State& st = S();
    Settings& set = GlobalSettings();
    CardBegin(h, "##verinst", "INSTALLED NEXT TO THIS VERSION");
    if (st.installed.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Nothing else is installed yet. When the launcher updates, it keeps the version it replaces here, so projects made with it can "
                            "still be opened. You can also install older versions from the list below.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 4));
    }
    for (const Installed& in : st.installed) {
        ImGui::PushID(in.version.c_str());
        const bool lts = st.haveIndex && st.index.ok && okayver::SameLine(in.version, st.index.lts);
        const bool latest = st.haveIndex && st.index.ok && in.version == st.index.latest;
        const bool isDefault = set.defaultEditor == in.version;
        Row r = RowBegin();
        if (in.version == st.highlight) RowHighlight(h, r);
        RowTitle(h, r, in.version, latest, lts, isDefault);
        std::string sub = in.source == "kept" ? "Kept when the launcher updated" : in.source == "installed" ? "Installed by you" : "Put here by hand";
        if (!in.date.empty()) sub += "  \xC2\xB7  " + in.date;
        if (lts) sub += "  \xC2\xB7  only gets fixes";
        const float btnW = 120.0f + 8.0f + 96.0f, sizeW = 92.0f;
        DrawTextAt(r.dl, ImVec2(r.p0.x, r.p0.y + 33), Fit(sub, r.width - btnW - sizeW - 24.0f).c_str(), ImGui::GetColorU32(ImGuiCol_TextDisabled));
        const std::string size = HumanSize(in.bytes);
        DrawTextAt(r.dl, ImVec2(r.p0.x + r.width - btnW - 14.0f - TextWidth(size.c_str()), r.p0.y + 19), size.c_str(), ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::SetCursorScreenPos(ImVec2(r.p0.x + r.width - btnW, r.p0.y + 14));
        ImGui::BeginDisabled(isDefault);
        if (ImGui::Button(isDefault ? "Default" : "Make default", ImVec2(120, 30))) { set.defaultEditor = in.version; if (h.savePrefs) h.savePrefs(); Toast(h, V(in.version) + " is now your default editor.", 1); }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 8);
        if (DangerButton("Remove", ImVec2(96, 30))) st.removeAsk = in.version;
        RowEnd(r, &in != &st.installed.back());
        ImGui::PopID();
    }

    // Keep previous versions: N.
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1, 1, 1, 0.06f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Keep previous versions:");
    ImGui::SameLine(0, 10);
    ImGui::SetNextItemWidth(72);
    int keep = std::clamp(set.keepPrevious, 0, kMaxKeepPrevious);
    if (ImGui::Combo("##keepprev", &keep, "0\0" "1\0" "2\0" "3\0" "4\0" "5\0" "6\0" "7\0" "8\0" "9\0")) {
        set.keepPrevious = keep;
        if (h.savePrefs) h.savePrefs();
    }
    ImGui::SameLine(0, 14);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("The Versions folder uses %s.", HumanSize(st.usage).c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("When the launcher updates it keeps the version it replaces, up to this many. Versions you install yourself are never removed "
                        "automatically.");
    ImGui::PopTextWrapPos();
    std::vector<std::string> keepList;
    if (!set.defaultEditor.empty()) keepList.push_back(set.defaultEditor);
    const std::string ltsHead = st.haveIndex && st.index.ok ? st.index.lts : set.ltsLine;
    const std::vector<std::string> prune = PlanPrune(st.installed, keep, ltsHead, keepList);
    if (!prune.empty()) {
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAmber), "%d kept %s over the limit will be removed the next time the launcher updates.",
                           (int)prune.size(), prune.size() == 1 ? "copy is" : "copies are");
        ImGui::SameLine(0, 12);
        if (ImGui::Button("Remove them now")) {
            const PruneResult pr = Prune(h.launcherDir, keep, ltsHead, MakeGuards(h));
            st.installedDirty = true;
            Toast(h, pr.failed.empty() ? "Removed " + std::to_string(pr.removed.size()) + " older " + (pr.removed.size() == 1 ? "version" : "versions") + "."
                                       : "Some versions could not be removed.", pr.failed.empty() ? 1 : 2);
        }
    }
    CardEnd();
}

std::uint64_t ListedSize(const Entry& e) {
    std::uint64_t n = 0;
    for (Asset a : {Asset::Editor, Asset::Player, Asset::Sdl}) {
        const auto it = e.files.find(IndexName(a));
        if (it != e.files.end()) n += it->second.size;
    }
    return n;
}

void DrawAvailable(const Host& h) {
    State& st = S();
    Settings& set = GlobalSettings();
    CardBegin(h, "##veravail", "AVAILABLE TO INSTALL");
    if (!st.haveIndex) {
        ImGui::TextDisabled("Looking up the version list\xE2\x80\xA6");
    } else if (!st.index.ok) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kRed), "%s", st.index.error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::BeginDisabled(st.indexJob != nullptr);
        if (ImGui::Button("Try again", ImVec2(120, 30))) StartIndex(h);
        ImGui::EndDisabled();
    } else {
        const std::string cur = okayver::NormalizeVersion(h.currentVersion);
        std::vector<const Entry*> rows;
        for (const Entry& e : st.index.versions) {
            if (e.version == cur) continue;
            bool have = false;
            for (const Installed& in : st.installed) if (in.version == e.version) have = true;
            if (!have) rows.push_back(&e);
        }
        if (rows.empty()) ImGui::TextDisabled("Every version in the list is installed or is the one you're running.");
        const std::size_t limit = st.showAllAvailable ? rows.size() : std::min<std::size_t>(rows.size(), 8);
        for (std::size_t i = 0; i < limit; ++i) {
            const Entry& e = *rows[i];
            ImGui::PushID(e.version.c_str());
            const bool mine = st.job && st.job->version == e.version;
            const auto note = st.notes.find(e.version);
            const bool hasNote = note != st.notes.end() && !mine;
            Row r = RowBegin(hasNote ? 84.0f : 58.0f);
            if (e.version == st.highlight) RowHighlight(h, r);
            const bool lts = e.version == st.index.lts, latest = e.version == st.index.latest;
            RowTitle(h, r, e.version, latest, lts, false);
            const float rightW = mine ? 330.0f : 130.0f;
            std::string sub = e.date.empty() ? std::string() : e.date;
            const std::uint64_t listed = ListedSize(e);
            if (listed) sub += (sub.empty() ? "" : "  \xC2\xB7  ") + std::string("about ") + HumanSize(listed);
            const bool checkable = !e.files.empty() && e.files.count(IndexName(Asset::Editor)) && !e.files.at(IndexName(Asset::Editor)).sha256.empty();
            sub += std::string(sub.empty() ? "" : "  \xC2\xB7  ") + (e.inBucket ? "from the OkaySpace download server" : "from GitHub");
            if (!checkable) sub += "  \xC2\xB7  no checksum: can't be installed here";
            if (lts) sub += "  \xC2\xB7  only gets fixes";
            if (mine) {
                std::string file;
                SDL_LockMutex(st.job->mu);
                file = st.job->file;
                SDL_UnlockMutex(st.job->mu);
                sub = st.job->cancel ? std::string("Cancelling\xE2\x80\xA6") : "Downloading " + file + " (" + std::to_string(st.job->fileIndex.load()) + " of " + std::to_string(st.job->fileCount.load()) + ")";
            }
            DrawTextAt(r.dl, ImVec2(r.p0.x, r.p0.y + 33), Fit(sub, r.width - rightW - 16.0f).c_str(), ImGui::GetColorU32(ImGuiCol_TextDisabled));
            if (mine) {
                const std::uint64_t done = st.job->doneBytes.load(), total = st.job->totalBytes.load();
                const ImVec2 barPos(r.p0.x + r.width - rightW, r.p0.y + 17);
                ImGui::SetCursorScreenPos(barPos);
                char overlay[64];
                if (total > 0) std::snprintf(overlay, sizeof(overlay), "%s / %s", HumanSize(std::min(done, total)).c_str(), HumanSize(total).c_str());
                else std::snprintf(overlay, sizeof(overlay), "%s", HumanSize(done).c_str());
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, h.accent);
                ImGui::ProgressBar(total > 0 ? std::min(1.0f, (float)((double)done / (double)total)) : 0.0f, ImVec2(rightW - 100.0f, 24), overlay);
                ImGui::PopStyleColor();
                ImGui::SetCursorScreenPos(ImVec2(barPos.x + rightW - 92.0f, barPos.y));
                ImGui::BeginDisabled(st.job->cancel.load());
                if (ImGui::Button("Cancel", ImVec2(84, 24))) st.job->cancel = true;
                ImGui::EndDisabled();
            } else {
                ImGui::SetCursorScreenPos(ImVec2(r.p0.x + r.width - 120.0f, r.p0.y + 14));
                ImGui::BeginDisabled(st.job != nullptr);
                if (AccentButton(h, "Install", ImVec2(120, 30))) BeginInstall(h, e.version);
                ImGui::EndDisabled();
            }
            if (hasNote) {
                const bool bad = note->second.second == 2;
                ImGui::SetCursorScreenPos(ImVec2(r.p0.x, r.p0.y + 56));
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + r.width);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(bad ? kRed : kGreen), "%s", note->second.first.c_str());
                ImGui::PopTextWrapPos();
            }
            RowEnd(r, i + 1 < limit);
            ImGui::PopID();
        }
        if (rows.size() > limit || st.showAllAvailable) {
            ImGui::Dummy(ImVec2(0, 4));
            if (ImGui::SmallButton(st.showAllAvailable ? "Show fewer" : ("Show " + std::to_string(rows.size() - limit) + " older versions").c_str())) st.showAllAvailable = !st.showAllAvailable;
        }
    }
    (void)set;
    CardEnd();
}

void DrawModals(const Host& h) {
    State& st = S();
    if (!st.removeAsk.empty() && !ImGui::IsPopupOpen("Remove this version?")) ImGui::OpenPopup("Remove this version?");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Remove this version?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const std::string v = st.removeAsk;
        const fs::path dir = okayver::VersionsDir(h.launcherDir) / v;
        std::uint64_t bytes = 0;
        for (const Installed& i : st.installed) if (i.version == v) bytes = i.bytes;
        ImGui::PushTextWrapPos(460.0f);
        ImGui::Text("Remove %s?", V(v).c_str());
        ImGui::TextDisabled("%s", dir.string().c_str());
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::Text("This frees %s. Your projects are not touched; projects made with %s can be opened with it again after you install it.", HumanSize(bytes).c_str(), okayver::LineLabel(v).c_str());
        if (h.editorRunning) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAmber), "An editor is open. A version that editor is using can't be removed.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 6));
        if (DangerButton("Remove", ImVec2(120, 0))) {
            const Result r = Remove(h.launcherDir, v, MakeGuards(h));
            Toast(h, r.message, r.ok() ? 1 : 2);
            st.removeAsk.clear();
            st.installedDirty = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { st.removeAsk.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen("Remove this version?")) st.removeAsk.clear();
}

}  // namespace

// ---- the interface ---------------------------------------------------------------------------------------------------------------------

void RequestPage(const std::string& version) {
    State& st = S();
    st.requested = okayver::NormalizeVersion(version);
    st.pageRequested = true;
    st.installedDirty = true;
    ResolveHighlight();
}
bool TakePageRequest() {
    State& st = S();
    const bool r = st.pageRequested;
    st.pageRequested = false;
    return r;
}
bool InstallRunning() { return S().job != nullptr; }

const std::vector<std::string>& InstalledVersionsCached(const fs::path& launcherDir) {
    State& st = S();
    if (st.installedDirty || okay::platform::TicksMs32() - st.installedAt > 2500) {
        st.installedVersions = okayver::InstalledVersions(launcherDir);
        st.installedAt = okay::platform::TicksMs32();
        // (the full list with sizes is refreshed by the page itself)
    }
    return st.installedVersions;
}
void InvalidateInstalled() { S().installedDirty = true; }

void Tick(const Host& h) {
    State& st = S();
    if (!st.staleCleaned) { st.staleCleaned = true; CleanStale(h.launcherDir); }
    if (st.indexJob && st.indexJob->done) {
        SDL_WaitThread(st.indexJob->th, nullptr);
        st.index = std::move(st.indexJob->result);
        st.indexJob.reset();
        st.haveIndex = true;
        st.indexAt = okay::platform::TicksMs32();
        if (st.index.ok && !st.index.lts.empty()) {
            Settings& set = GlobalSettings();
            const std::string line = okayver::LineOf(st.index.lts);
            if (set.ltsLine != line) { set.ltsLine = line; if (h.savePrefs) h.savePrefs(); }
        }
        ResolveHighlight();
    }
    if (st.job && st.job->done) {
        SDL_WaitThread(st.job->th, nullptr);
        const Result r = st.job->result;
        const std::string v = st.job->version;
        st.job.reset();
        st.installedDirty = true;
        switch (r.outcome) {
            case Outcome::Installed:
                st.notes.erase(v);
                Toast(h, "Installed " + V(v) + ".", 1);
                break;
            case Outcome::Cancelled:
                st.notes.erase(v);
                Toast(h, "Cancelled. Nothing was installed.", 0);
                break;
            case Outcome::AlreadyInstalled: st.notes.erase(v); break;
            default:
                st.notes[v] = {r.message, 2};
                Toast(h, r.message, 2);
                break;
        }
    }
}

void DrawPage(const Host& h) {
    State& st = S();
    if (!st.haveIndex && !st.indexJob) StartIndex(h);
    if (st.installedDirty || okay::platform::TicksMs32() - st.installedAt > 10000) RefreshInstalled(h);
    DrawRequestBanner(h);
    DrawThisVersion(h);
    DrawChannel(h);
    DrawInstalled(h);
    DrawAvailable(h);
    DrawModals(h);
}

bool DrawChannelCombo(const Host& h, float width) {
    Settings& set = GlobalSettings();
    int idx = set.channel == Channel::Lts ? 1 : 0;
    ImGui::SetNextItemWidth(width);
    if (!ImGui::Combo("##updchannel", &idx, "Latest\0Long-term support (LTS)\0")) return false;
    set.channel = idx == 1 ? Channel::Lts : Channel::Latest;
    if (h.savePrefs) h.savePrefs();
    if (h.channelChanged) h.channelChanged();
    return true;
}

void Shutdown() {
    State& st = S();
    if (st.job) {
        st.job->cancel = true;
        SDL_WaitThread(st.job->th, nullptr);
        st.job.reset();
    }
    if (st.indexJob) {
        SDL_WaitThread(st.indexJob->th, nullptr);
        st.indexJob.reset();
    }
}

}  // namespace okayversionsui
