// Anti-cheat backend pages (AntiCheatPages.hpp): Admin > Anti-cheat (reports, bans, appeals), the player's own ban and
// appeal, "Report cheating", and the session-ticket check before a join. Moved out of launcher/main.cpp unchanged
// (docs/engine/modularization_plan.md, Part C section 19, batch L2).
#include "LauncherShared.hpp"
#include "NavIcons.hpp"   // SlateCombo, the launcher's dropdown
#include "AntiCheatPages.hpp"
#include "AntiCheatBackend.hpp"     // anti-cheat reports / bans / appeals
#include "AntiCheatAdminTools.hpp"  // linked accounts, ban with devices, report evidence
#include "AntiCheatHardware.hpp"    // the salted hardware hash sent with appeals
#include "TicketClient.hpp"         // session tickets: the ban / ticket check before a join
#include "OfficialGame.hpp"
#include "OkayTheme.hpp"
#include "okay/Platform/Platform.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Net/JoinLink.hpp"

namespace okl {


// ---- Anti-cheat backend (docs/sql/anticheat.sql, docs/online/anticheat.md) ----------
// Admin > Anti-cheat (admins): review reports, ban / unban accounts and
// machines, decide appeals. Players: a banner for their own anti-cheat ban with
// an "Appeal" flow, and "Report cheating" in a chat's menu. Requests are built
// and parsed by shared/AntiCheatBackend.hpp; the server's RLS decides who may
// do what. OKAY_UI_DEMO_USER fills everything with sample data (no network).

struct AcAdminData {
    std::vector<acb::Report> reports;
    std::vector<acb::Ban> bans;
    std::vector<acb::Appeal> appeals;
    std::map<std::string, std::string> names;   // uid -> username
    std::string linkedFor;                      // the account "Linked accounts" was asked for ("" = none)
    std::vector<acb::LinkedAccount> linked;
    long evidenceFor = 0;                       // the report "Evidence" was asked for (0 = none)
    std::string evidence;                       // its bundle JSON ("" = none attached)
    std::string loadError;                      // "" = loaded
};
struct AcAdminJob {
    bool hasAction = false;
    acb::Request action;
    std::string okMsg;
    bool countBans = false;     // the action is ac_ban_with_devices: add "(N bans)" to the message
    std::string linkedFor;      // fetch ac_linked_accounts for this account (kept across refreshes)
    long evidenceFor = 0;       // fetch this report's evidence bundle
    std::string reportStatus;   // the Reports filter ("" = all)
    std::string banUser;        // "Ban a player": look this username up, then ban it with:
    std::string banReason; long banUntil = 0;
};
static okay::thr::Mutex* g_acAdMutex = nullptr;
static volatile bool g_acAdBusy = false;
static AcAdminData g_acAdNew; static bool g_acAdReady = false;   // (guarded)
static std::string g_acAdMsg; static bool g_acAdMsgErr = false;   // (guarded)

static int AcAdminWorker(void* p) {
    std::unique_ptr<AcAdminJob> j(static_cast<AcAdminJob*>(p));
    AcAdminData d;
    std::string msg; bool err = false;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        if (!j->banUser.empty()) {   // ban by username: find the id, then ban
            const auto r = acb::Send(a, acb::UserByName(j->banUser));
            std::map<std::string, std::string> found;
            std::string uid;
            if (r.ok && acb::ParseUsernames(r.body, found) && !found.empty()) uid = found.begin()->first;
            if (uid.empty()) { msg = "No player called " + j->banUser; err = true; }
            else j->hasAction = acb::BanPlayer(uid, std::string(), j->banReason, j->banUntil, 0, (long)std::time(nullptr), j->action, &msg);
            if (!uid.empty() && !j->hasAction) err = true;
        }
        if (j->hasAction) {
            const auto r = acb::Send(a, j->action);
            msg = r.ok ? j->okMsg : acb::ErrorText(r.status, r.body);
            err = !r.ok;
            int made = 0;
            if (r.ok && j->countBans && acb::ParseBanCount(r.body, made)) msg += " (" + std::to_string(made) + (made == 1 ? " ban)" : " bans)");
        }
        if (!j->linkedFor.empty()) {
            acb::Request q;
            if (acb::LinkedAccounts(j->linkedFor, q)) {
                const auto r = acb::Send(a, q);
                if (r.ok && acb::ParseLinkedAccounts(r.body, d.linked)) d.linkedFor = j->linkedFor;
                else { msg = acb::ErrorText(r.status, r.body); err = true; }
            }
        }
        if (j->evidenceFor > 0) {
            acb::Request q;
            if (acb::ReportEvidence(j->evidenceFor, q)) {
                const auto r = acb::Send(a, q);
                if (r.ok) { d.evidence = acb::ParseReportEvidence(r.body); d.evidenceFor = j->evidenceFor; if (d.evidence.empty()) { msg = "No evidence bundle on that report"; } }
                else { msg = acb::ErrorText(r.status, r.body); err = true; }
            }
        }
        const long now = (long)std::time(nullptr);
        auto load = [&](const acb::Request& q, auto parse, auto& into) {
            const auto r = acb::Send(a, q);
            if (!r.ok || !parse(r.body, into)) { if (d.loadError.empty()) d.loadError = acb::ErrorText(r.status, r.body); }
        };
        load(acb::ListReports(j->reportStatus, 200), acb::ParseReports, d.reports);
        load(acb::ListBans(false, now), acb::ParseBans, d.bans);
        load(acb::ListAppeals(""), acb::ParseAppeals, d.appeals);
        std::vector<std::string> ids;
        for (const auto& x : d.reports) { ids.push_back(x.reporter); ids.push_back(x.reported); }
        for (const auto& x : d.bans) ids.push_back(x.userId);
        for (const auto& x : d.appeals) ids.push_back(x.userId);
        for (const auto& x : d.linked) ids.push_back(x.userId);
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        for (std::size_t i = 0; i < ids.size(); i += 80) {   // keep each URL short
            std::vector<std::string> part(ids.begin() + (long)i, ids.begin() + (long)std::min(ids.size(), i + 80));
            const auto r = acb::Send(a, acb::Usernames(part));
            if (r.ok) acb::ParseUsernames(r.body, d.names);
        }
    } catch (...) { msg = "Something went wrong"; err = true; }
    okay::thr::LockMutex(g_acAdMutex);
    g_acAdNew = std::move(d); g_acAdReady = true;
    if (!msg.empty()) { g_acAdMsg = msg; g_acAdMsgErr = err; }
    okay::thr::UnlockMutex(g_acAdMutex);
    g_acAdBusy = false;
    return 0;
}

static AcAdminData DemoAcAdminData() {
    AcAdminData d;
    const long t0 = (long)std::time(nullptr), h = 3600, day = 86400;
    const std::string uMaya = "0b5a4c1e-0000-4000-8000-000000000001", uTim = "0b5a4c1e-0000-4000-8000-000000000002",
                      uLeo = "0b5a4c1e-0000-4000-8000-000000000003", uZed = "0b5a4c1e-0000-4000-8000-000000000004",
                      uKai = "0b5a4c1e-0000-4000-8000-000000000005";
    d.names = {{uMaya, "maya"}, {uTim, "toxic_tim"}, {uLeo, "leo"}, {uZed, "zed_aims"}, {uKai, "kai"}};
    auto rep = [&](long id, const std::string& by, const std::string& who, const char* kind, const char* game, const char* why,
                   const char* det, const char* st, long ago, bool hw) {
        acb::Report r; r.id = id; r.reporter = by; r.reported = who; r.kind = kind; r.game = game; r.reason = why; r.details = det;
        r.status = st; r.created = t0 - ago; if (hw) r.hwHash = "3f9c2a7be41d0c58a6e7f1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f6";
        d.reports.push_back(r); };
    rep(31, uZed, "", "client_check", "Neon Drift", "clock", "the game clock runs 41.8% faster than the system clock", "open", 25 * 60, true);
    rep(30, uMaya, uZed, "player", "Neon Drift", "Aimbot", "Snapped to heads through smoke, every round. Match at 14:20.", "open", 2 * h, false);
    rep(29, uZed, "", "client_check", "Neon Drift", "module", "unexpected module loaded: C:\\Users\\zed\\AppData\\Local\\Temp\\nd_trainer.dll", "open", 3 * h, true);
    rep(28, uLeo, uTim, "player", "Kart Rush", "Wall hacks", "Always knew where I was hiding.", "reviewing", 20 * h, false);
    rep(27, uKai, "", "client_check", "Star Arena", "integrity", "Data/game.okayscene was modified", "dismissed", 2 * day, true);
    auto ban = [&](long id, const std::string& uid, const char* why, long until, long ago, bool lifted, bool hw, long report) {
        acb::Ban b; b.id = id; b.userId = uid; b.reason = why; b.until = until; b.created = t0 - ago; b.liftedFlag = lifted;
        if (lifted) { b.lifted = t0 - ago / 2; b.liftReason = "appeal accepted"; }
        if (hw) b.hwHash = "3f9c2a7be41d0c58a6e7f1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f6";
        b.reportId = report; d.bans.push_back(b); };
    ban(12, uZed, "Speedhack (clock 41.8% fast) and a trainer DLL", 0, 20 * 60, false, true, 31);
    ban(11, uTim, "Wall hacks in Kart Rush", t0 + 6 * day, day, false, false, 28);
    ban(10, uKai, "Edited game files", 0, 3 * day, true, false, 27);
    auto ap = [&](long id, long banId, const std::string& uid, const char* msg, const char* st, long ago, const char* note = "") {
        acb::Appeal a; a.id = id; a.banId = banId; a.userId = uid; a.message = msg; a.status = st; a.created = t0 - ago;
        a.decisionNote = note; if (std::string(st) != "open") a.decided = t0 - ago / 2; d.appeals.push_back(a); };
    ap(5, 11, uTim, "I was spectating my friend's screen on Discord, that's how I knew. Please check the replay.", "open", 5 * h);
    ap(4, 10, uKai, "The antivirus quarantined a file and I restored an old copy. Reinstalled now.", "accepted", 2 * day, "Reinstall checks out");
    return d;
}

// Text for a ban's end.
static std::string AcUntil(long until) { return until == 0 ? std::string("permanent") : "until " + FormatDate(until); }

void DrawAntiCheatAdmin() {
    static const bool kDemo = std::getenv("OKAY_UI_DEMO_USER") != nullptr;
    static int view = std::getenv("OKAY_UI_DEMO_ACVIEW") ? std::atoi(std::getenv("OKAY_UI_DEMO_ACVIEW")) : 0;   // (env: dev screenshot hook)
    static AcAdminData D;
    static bool loaded = false, firstFrame = true;
    static int repFilter = 0;          // Open, Reviewing, Actioned, Dismissed, All
    static long selReport = 0;
    static std::string linkedUid;      // "Linked accounts" shows for this account (re-fetched with every refresh)
    static long evidenceId = 0;        // "Evidence" shows for this report
    static bool activeOnly = true;
    static char note[300] = "";
    // The ban dialog.
    static bool banOpen = false;
    static std::string banUid, banHw, banWho; static long banReport = 0;
    static char banReason[300] = "", banName[64] = "";
    static int banDur = 0; static bool banMachine = true, banDevices = false;
    // The unban / decide dialogs.
    static long unbanId = 0; static char unbanWhy[300] = "";
    static long decideId = 0; static bool decideAccept = true; static char decideNote[300] = "";
    static const char* kRepStatus[] = {"open", "reviewing", "actioned", "dismissed", ""};
    const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text), cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    if (!g_acAdMutex) g_acAdMutex = okay::thr::CreateMutex();
    if (kDemo && !loaded) { loaded = true; D = DemoAcAdminData(); }

    auto queue = [&](AcAdminJob* j) {
        if (kDemo) { delete j; Toast("Demo: nothing is sent", 0); return; }
        if (g_acAdBusy) { delete j; Toast("Still working on the last action\xE2\x80\xA6", 0); return; }
        j->reportStatus = kRepStatus[repFilter];
        if (j->linkedFor.empty()) j->linkedFor = linkedUid;
        if (!j->evidenceFor && evidenceId) j->evidenceFor = evidenceId;
        g_acAdBusy = true;
        if (okay::thr::Thread* t = okay::thr::CreateThread(AcAdminWorker, "okay-acadmin", j)) okay::thr::DetachThread(t);
        else { g_acAdBusy = false; delete j; }
    };
    auto act = [&](const acb::Request& r, const char* okMsg) { AcAdminJob* j = new AcAdminJob; j->hasAction = true; j->action = r; j->okMsg = okMsg; queue(j); };
    if (!kDemo) {
        if (firstFrame) { firstFrame = false; queue(new AcAdminJob); }
        okay::thr::LockMutex(g_acAdMutex);
        if (g_acAdReady) { g_acAdReady = false; D = g_acAdNew; loaded = true; }   // (linkedUid / evidenceId are re-fetched by the next job)
        if (!g_acAdMsg.empty()) { Toast(g_acAdMsg, g_acAdMsgErr ? 2 : 1); g_acAdMsg.clear(); }
        okay::thr::UnlockMutex(g_acAdMutex);
    }
    auto name = [&](const std::string& uid) -> std::string {
        if (uid.empty()) return std::string();
        auto it = D.names.find(uid);
        return it != D.names.end() && !it->second.empty() ? "@" + it->second : uid.substr(0, 8);
    };
    const long now = (long)std::time(nullptr);
    auto card = [&](const char* id) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
        ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    };
    auto cardEnd = [&]() { ImGui::EndChild(); ImGui::PopStyleVar(2); ImGui::PopStyleColor(); };
    auto pill = [&](const char* t, ImU32 bg) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos(), ts = ImGui::CalcTextSize(t);
        const float s = 0.78f, w = ts.x * s + 12;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + ts.y * s + 6), bg, 8.0f);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * s, ImVec2(p.x + 6, p.y + 3), IM_COL32_WHITE, t);
        ImGui::Dummy(ImVec2(w, ts.y * s + 6));
    };
    auto statusCol = [](const std::string& s) -> ImU32 {
        return s == "open" ? okaytheme::WarningFillU32() : s == "reviewing" ? okaytheme::U32(kAccent)
             : s == "actioned" || s == "accepted" ? okaytheme::SuccessFillU32() : okaytheme::TextDimU32();
    };
    auto openBan = [&](const std::string& uid, const std::string& hw, long report, const std::string& reason) {
        banOpen = true; banUid = uid; banHw = hw; banReport = report; banWho = uid.empty() ? std::string() : name(uid);
        std::snprintf(banReason, sizeof(banReason), "%s", reason.c_str()); banName[0] = '\0'; banDur = 0; banMachine = !hw.empty(); banDevices = false;
    };

    // ---- Views ----
    int nOpen = 0, nAppeals = 0, nBans = 0;
    for (const auto& r : D.reports) nOpen += r.status == "open";
    for (const auto& a : D.appeals) nAppeals += a.status == "open";
    for (const auto& b : D.bans) nBans += b.Active(now);
    {
        char l0[48], l1[48], l2[48];
        std::snprintf(l0, sizeof(l0), "Reports  %d", nOpen);
        std::snprintf(l1, sizeof(l1), "Bans  %d", nBans);
        std::snprintf(l2, sizeof(l2), "Appeals  %d", nAppeals);
        const char* labels[] = {l0, l1, l2};
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine();
            const bool on = view == i;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
            if (ImGui::Button(labels[i], ImVec2(130, 30))) view = i;
            if (on) ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - 250));
        if (ImGui::Button("Ban a player\xE2\x80\xA6", ImVec2(130, 30))) openBan("", "", 0, "");
        ImGui::SameLine();
        if (ImGui::Button(g_acAdBusy ? "Loading\xE2\x80\xA6" : "Refresh", ImVec2(110, 30))) queue(new AcAdminJob);
        ImGui::Dummy(ImVec2(0, 6));
    }
    if (!D.loadError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, okaytheme::Current().danger);
        ImGui::TextWrapped("%s", D.loadError.c_str());
        ImGui::PopStyleColor();
    }

    if (view == 0) {   // ======================= Reports =======================
        ImGui::SetNextItemWidth(150);
        if (SlateCombo("##acrepf", &repFilter, "Open\0Reviewing\0Actioned\0Dismissed\0All\0")) { selReport = 0; queue(new AcAdminJob); }
        ImGui::SameLine();
        ImGui::TextDisabled("Player reports, and what players' own anti-cheat found (client checks). Reports go after 90 days.");
        ImGui::Dummy(ImVec2(0, 4));
        card("##acreps");
        int shown = 0;
        for (const auto& r : D.reports) {
            if (repFilter < 4 && r.status != kRepStatus[repFilter]) continue;
            ++shown;
            ImGui::PushID((int)r.id);
            const bool sel = selReport == r.id;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##acrep", sel, 0, ImVec2(0, 44))) selReport = sel ? 0 : r.id;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(p.x + 6, p.y + 4), cDim, RelTime(r.created).c_str());
            const bool client = r.kind == "client_check";
            const std::string who = client ? name(r.reporter) + "'s game" : name(r.reported);
            dl->AddText(ImVec2(p.x + 96, p.y + 4), cText, who.c_str());
            const std::string what = (client ? "[" + r.reason + "] " + r.details : r.reason + (r.game.empty() ? "" : "  \xC2\xB7  " + r.game));
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + 96, p.y + 23), cDim, what.c_str(), nullptr, 0);
            const std::string tag = client ? "client check" : "by " + name(r.reporter);
            const ImVec2 ts = ImGui::CalcTextSize(r.status.c_str());
            const float rx = p.x + ImGui::GetContentRegionAvail().x;
            dl->AddRectFilled(ImVec2(rx - ts.x - 18, p.y + 4), ImVec2(rx - 4, p.y + 8 + ts.y), statusCol(r.status), 8.0f);
            dl->AddText(ImVec2(rx - ts.x - 11, p.y + 6), IM_COL32_WHITE, r.status.c_str());
            dl->AddText(ImVec2(rx - ImGui::CalcTextSize(tag.c_str()).x - 6, p.y + 24), cDim, tag.c_str());
            if (sel) {
                ImGui::Indent(96);
                ImGui::PushTextWrapPos(0.0f);
                if (!r.details.empty() && !client) ImGui::TextUnformatted(r.details.c_str());
                if (!r.hwHash.empty()) ImGui::TextDisabled("Machine %s\xE2\x80\xA6 (salted hash; no raw hardware ids)", r.hwHash.substr(0, 12).c_str());
                if (!r.reviewNote.empty()) ImGui::TextDisabled("Note: %s", r.reviewNote.c_str());
                ImGui::PopTextWrapPos();
                ImGui::SetNextItemWidth(260);
                ImGui::InputTextWithHint("##acnote", "Note (optional)", note, sizeof(note));
                ImGui::SameLine();
                acb::Request q;
                if (r.status != "reviewing" && ImGui::Button("Reviewing") && acb::SetReportStatus(r.id, "reviewing", note, q)) { act(q, "Marked as reviewing"); note[0] = '\0'; }
                ImGui::SameLine();
                if (r.status != "dismissed" && ImGui::Button("Dismiss") && acb::SetReportStatus(r.id, "dismissed", note, q)) { act(q, "Report dismissed"); note[0] = '\0'; }
                ImGui::SameLine();
                const std::string subject = client ? r.reporter : r.reported;
                if (ImGui::Button("Linked accounts") && acb::IsUuid(subject)) {
                    linkedUid = subject;
                    AcAdminJob* j = new AcAdminJob; j->linkedFor = subject; queue(j);
                }
                if (r.kind == "server_kick") {
                    ImGui::SameLine();
                    if (ImGui::Button("Evidence")) { evidenceId = r.id; AcAdminJob* j = new AcAdminJob; j->evidenceFor = r.id; queue(j); }
                }
                ImGui::SameLine();
                if (PrimaryButton("Ban\xE2\x80\xA6", ImVec2(80, 0)))
                    openBan(client ? r.reporter : r.reported, client ? r.hwHash : std::string(), r.id,
                            client ? r.reason + ": " + r.details : r.reason);
                if (linkedUid == subject && D.linkedFor == subject) {
                    ImGui::TextDisabled("Accounts that used a machine this account used (a lead, not proof: machines can be shared):");
                    if (D.linked.empty()) ImGui::TextDisabled("None.");
                    for (const auto& l : D.linked)
                        ImGui::BulletText("%s  \xC2\xB7  %s  \xC2\xB7  last seen %s", name(l.userId).c_str(), acb::LinkedSummary(l).c_str(),
                                          l.lastSeen ? RelTime(l.lastSeen).c_str() : "-");
                }
                if (evidenceId == r.id && D.evidenceFor == r.id && !D.evidence.empty()) {
                    ImGui::TextDisabled("Detection evidence (no account or address unless the server opted in):");
                    ImGui::TextWrapped("%s", D.evidence.c_str());
                }
                ImGui::Unindent(96);
                ImGui::Dummy(ImVec2(0, 4));
            }
            ImGui::PopID();
        }
        if (!shown) ImGui::TextDisabled(loaded ? "Nothing here." : "Loading\xE2\x80\xA6");
        cardEnd();
    } else if (view == 1) {   // ======================= Bans =======================
        ImGui::Checkbox("Active only", &activeOnly);
        ImGui::SameLine();
        ImGui::TextDisabled("Anti-cheat bans (separate from site bans). Unbanning keeps the record; ended bans go after 90 days.");
        ImGui::Dummy(ImVec2(0, 4));
        card("##acbans");
        int shown = 0;
        for (const auto& b : D.bans) {
            const bool active = b.Active(now);
            if (activeOnly && !active) continue;
            ++shown;
            ImGui::PushID((int)b.id);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const std::string who = (b.userId.empty() ? std::string("machine ") + b.hwHash.substr(0, 10) : name(b.userId)) +
                                    (!b.userId.empty() && !b.hwHash.empty() ? "  + machine" : "");
            dl->AddText(ImVec2(p.x, p.y + 2), cText, who.c_str());
            dl->AddText(ImVec2(p.x, p.y + 21), cDim, (b.reason + "  \xC2\xB7  " + AcUntil(b.until) + "  \xC2\xB7  " + RelTime(b.created) +
                                                      (b.lifted ? "  \xC2\xB7  lifted: " + (b.liftReason.empty() ? std::string("-") : b.liftReason)
                                                                : !active ? std::string("  \xC2\xB7  expired") : std::string())).c_str());
            ImGui::Dummy(ImVec2(0, 42));
            if (active) {
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90);
                ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p.y + 6));
                if (ImGui::Button("Unban\xE2\x80\xA6", ImVec2(90, 28))) { unbanId = b.id; unbanWhy[0] = '\0'; }
            }
            ImGui::PopID();
        }
        if (!shown) ImGui::TextDisabled(loaded ? "No bans." : "Loading\xE2\x80\xA6");
        cardEnd();
    } else {   // ======================= Appeals =======================
        ImGui::TextDisabled("Players appeal their own bans (3 appeals per ban). Accepting lifts the ban.");
        ImGui::Dummy(ImVec2(0, 4));
        card("##acapps");
        if (D.appeals.empty()) ImGui::TextDisabled(loaded ? "No appeals." : "Loading\xE2\x80\xA6");
        for (const auto& a : D.appeals) {
            ImGui::PushID((int)a.id);
            const acb::Ban* ban = nullptr;
            for (const auto& b : D.bans) if (b.id == a.banId) ban = &b;
            ImGui::TextUnformatted(name(a.userId).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s  \xC2\xB7  ban #%ld%s", RelTime(a.created).c_str(), a.banId,
                                ban ? ("  \xC2\xB7  " + ban->reason + ", " + AcUntil(ban->until)).c_str() : "");
            ImGui::SameLine();
            pill(a.status.c_str(), statusCol(a.status));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(a.message.c_str());
            if (!a.decisionNote.empty()) ImGui::TextDisabled("Decision: %s", a.decisionNote.c_str());
            ImGui::PopTextWrapPos();
            if (a.status == "open") {
                if (PrimaryButton("Accept\xE2\x80\xA6", ImVec2(100, 28))) { decideId = a.id; decideAccept = true; decideNote[0] = '\0'; }
                ImGui::SameLine();
                if (ImGui::Button("Reject\xE2\x80\xA6", ImVec2(100, 28))) { decideId = a.id; decideAccept = false; decideNote[0] = '\0'; }
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        cardEnd();
    }

    // ---- Dialogs ----
    if (std::getenv("OKAY_UI_DEMO_ACPOPUP") && kDemo) {   // (env: dev screenshot hook)
        static bool once = false;
        if (!once) { once = true; const int w = std::atoi(std::getenv("OKAY_UI_DEMO_ACPOPUP"));
            if (w == 1 && !D.reports.empty()) openBan(D.reports[0].reporter, D.reports[0].hwHash, D.reports[0].id, "Speedhack: " + D.reports[0].details);
            if (w == 2 && !D.bans.empty()) { unbanId = D.bans[1].id; std::snprintf(unbanWhy, sizeof(unbanWhy), "Replay shows no wall hacks"); }
            if (w == 3 && !D.appeals.empty()) { decideId = D.appeals[0].id; decideAccept = true; std::snprintf(decideNote, sizeof(decideNote), "Replay checks out"); } }
    }
    if (banOpen) { ImGui::OpenPopup("Anti-cheat ban"); banOpen = false; }
    if (ImGui::BeginPopupModal("Anti-cheat ban", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (!banUid.empty()) ImGui::Text("Ban %s", banWho.c_str());
        else { ImGui::SetNextItemWidth(320); ImGui::InputTextWithHint("##acbanname", "Username", banName, sizeof(banName)); }
        if (banReport) { ImGui::SameLine(); ImGui::TextDisabled("(from report #%ld)", banReport); }
        ImGui::SetNextItemWidth(420);
        ImGui::InputTextWithHint("##acbanwhy", "Reason (the player sees it)", banReason, sizeof(banReason));
        ImGui::SetNextItemWidth(200);
        SlateCombo("Length", &banDur, "Permanent\0" "1 day\0" "7 days\0" "30 days\0" "1 year\0");
        if (acb::IsHwHash(banHw)) ImGui::Checkbox("Also ban this machine (salted hardware hash)", &banMachine);
        if (!banUid.empty()) ImGui::Checkbox("Also ban every machine this account used (ac_ban_with_devices)", &banDevices);
        ImGui::Dummy(ImVec2(0, 4));
        acb::Request q; std::string err;
        const std::string hw = banMachine && acb::IsHwHash(banHw) ? banHw : std::string();
        if (PrimaryButton("Ban", ImVec2(110, 30))) {
            const bool byName = banUid.empty();
            const std::string uname = okayofficial::Trim(banName);
            if (byName && uname.empty()) Toast("Type the player's username", 2);
            else if (byName && okayofficial::Trim(banReason).empty()) Toast("Give a reason: the player sees it.", 2);
            else if (byName) {   // the worker looks the name up, then bans that id
                AcAdminJob* j = new AcAdminJob;
                j->banUser = uname; j->banReason = banReason; j->banUntil = acb::ExpiryFor(banDur, now); j->okMsg = "Banned";
                queue(j);
                ImGui::CloseCurrentPopup();
            } else if (banDevices) {   // the account and every machine it used, in one step
                if (!acb::BanWithDevices(banUid, banReason, acb::ExpiryFor(banDur, now), banReport, now, q, &err)) Toast(err, 2);
                else {
                    AcAdminJob* j = new AcAdminJob; j->hasAction = true; j->action = q; j->okMsg = "Banned"; j->countBans = true;
                    queue(j); ImGui::CloseCurrentPopup();
                }
            } else if (!acb::BanPlayer(banUid, hw, banReason, acb::ExpiryFor(banDur, now), banReport, now, q, &err)) Toast(err, 2);
            else { act(q, "Banned"); ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (unbanId) { ImGui::OpenPopup("Lift anti-cheat ban"); }
    if (ImGui::BeginPopupModal("Lift anti-cheat ban", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("Lift ban #%ld?", unbanId);
        ImGui::SetNextItemWidth(380);
        ImGui::InputTextWithHint("##acunwhy", "Why (kept with the ban)", unbanWhy, sizeof(unbanWhy));
        acb::Request q;
        if (PrimaryButton("Unban", ImVec2(110, 30))) { if (acb::Unban(unbanId, unbanWhy, q)) act(q, "Ban lifted"); unbanId = 0; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 30))) { unbanId = 0; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (decideId) { ImGui::OpenPopup("Decide appeal"); }
    if (ImGui::BeginPopupModal("Decide appeal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text(decideAccept ? "Accept appeal #%ld and lift the ban?" : "Reject appeal #%ld? The ban stays.", decideId);
        ImGui::SetNextItemWidth(380);
        ImGui::InputTextWithHint("##acdecnote", "Note for the player (optional)", decideNote, sizeof(decideNote));
        acb::Request q;
        if (PrimaryButton(decideAccept ? "Accept" : "Reject", ImVec2(110, 30))) {
            if (acb::DecideAppeal(decideId, decideAccept, decideNote, q)) act(q, decideAccept ? "Appeal accepted, ban lifted" : "Appeal rejected");
            decideId = 0; ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 30))) { decideId = 0; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

// ---- The player's side: my anti-cheat bans + appeal, and reporting a cheater ----
static okay::thr::Mutex* g_acMeMutex = nullptr;
static volatile bool g_acMeBusy = false;
static std::vector<acb::MyBan> g_acMyBans, g_acMyBansNew; static bool g_acMyBansReady = false;   // (New: guarded)
static std::string g_acMeMsg; static bool g_acMeMsgErr = false;                                  // (guarded)
static int AcMeWorker(void* p) {
    std::unique_ptr<AcMeJob> j(static_cast<AcMeJob*>(p));
    std::vector<acb::MyBan> bans; bool got = false;
    std::string msg; bool err = false;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        if (j->hasAction) {
            const auto r = acb::Send(a, j->action);
            msg = r.ok ? j->okMsg : acb::ErrorText(r.status, r.body); err = !r.ok;
        }
        const auto r = acb::Send(a, acb::MyBans(okayac::MachineHash()));
        got = r.ok && acb::ParseMyBans(r.body, bans);   // (not set up on the server: no banner, quietly)
    } catch (...) {}
    okay::thr::LockMutex(g_acMeMutex);
    if (got) { g_acMyBansNew = std::move(bans); g_acMyBansReady = true; }
    if (!msg.empty()) { g_acMeMsg = msg; g_acMeMsgErr = err; }
    okay::thr::UnlockMutex(g_acMeMutex);
    g_acMeBusy = false;
    return 0;
}
void AcMeQueue(AcMeJob* j) {
    if (!g_acMeMutex) g_acMeMutex = okay::thr::CreateMutex();
    if (g_acMeBusy) { delete j; Toast("One moment\xE2\x80\xA6", 0); return; }
    g_acMeBusy = true;
    if (okay::thr::Thread* t = okay::thr::CreateThread(AcMeWorker, "okay-acme", j)) okay::thr::DetachThread(t);
    else { g_acMeBusy = false; delete j; }
}
// Every frame: my anti-cheat bans at sign-in and every 5 minutes.
void PumpAcMyBans(bool signedIn) {
    static Uint32 next = 0; static bool wasIn = false;
    if (std::getenv("OKAY_UI_DEMO_USER")) {
        static bool seeded = false;
        if (!seeded && std::getenv("OKAY_UI_DEMO_ACBAN")) {   // (env: dev screenshot hook)
            seeded = true;
            acb::MyBan b; b.id = 12; b.reason = "Speedhack (clock 41.8% fast) and a trainer DLL"; b.created = (long)std::time(nullptr) - 1200;
            b.appealsLeft = 3; b.appealStatus = std::atoi(std::getenv("OKAY_UI_DEMO_ACBAN")) == 2 ? "rejected" : "";
            if (!b.appealStatus.empty()) { b.appealNote = "The trainer DLL was loaded in every match."; b.appealsLeft = 2; }
            g_acMyBans = {b};
        }
        return;
    }
    if (!g_acMeMutex) g_acMeMutex = okay::thr::CreateMutex();
    if (!signedIn) { g_acMyBans.clear(); wasIn = false; return; }
    const Uint32 now = okay::platform::TicksMs32();
    if (!wasIn) { wasIn = true; next = 0; }
    if (now >= next && !g_acMeBusy && !g_pollCfgDir.empty()) { next = now + 300000; AcMeQueue(new AcMeJob); }
    okay::thr::LockMutex(g_acMeMutex);
    if (g_acMyBansReady) { g_acMyBansReady = false; g_acMyBans = g_acMyBansNew; }
    if (!g_acMeMsg.empty()) { Toast(g_acMeMsg, g_acMeMsgErr ? 2 : 1); g_acMeMsg.clear(); }
    okay::thr::UnlockMutex(g_acMeMutex);
}


// ---- Session tickets before a join: the ban check + "no ticket, no join" --------------------------------------------------------
// okay_tickets.cfg (next to the launcher, or next to the game's scene) turns this on (shared/TicketClient.hpp). The launcher asks the ticket
// service on a worker thread before it starts a game to join a server: a banned answer refreshes my bans (the banner shows why) and the game
// is not started; with `mode required` any failure stops the start too (the game would refuse to join anyway, this just says why now).
// The game does the real, connection-bound request itself (player/main.cpp); this one is only the early answer, and its ticket is dropped.
static okay::thr::Mutex* g_tkMutex = nullptr;
static volatile bool g_tkBusy = false;
static std::string g_tkLink;                               // the link being checked (guarded)
static std::string g_tkOkLink;                             // a link whose check passed: the next MpLaunch of it goes through (UI thread)
static bool g_tkDone = false, g_tkBanned = false, g_tkAllow = false; static std::string g_tkMsg;   // the worker's answer (guarded)
static okayac::ticket::Config TicketConfigFor(const std::string& scene) {
    okayac::ticket::Config c = okayac::ticket::LoadConfig((fs::path(g_exeDir) / okayac::ticket::kConfigName).string());
    if (c.mode == okayac::ticket::Mode::Off && !scene.empty()) {
        std::error_code ec;
        const fs::path beside = fs::path(scene).parent_path() / okayac::ticket::kConfigName;
        if (fs::is_regular_file(beside, ec)) c = okayac::ticket::LoadConfig(beside.string());
    }
    return c;
}
struct TkJob { okayac::ticket::Config cfg; okay::JoinLink link; };
static int TicketPreflightWorker(void* p) {
    std::unique_ptr<TkJob> j(static_cast<TkJob*>(p));
    bool allow = true, banned = false; std::string msg;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okayac::ticket::Options opt;
        opt.refreshSignIn = [&a] { return a.ReloadSessionToken(); };
        okayac::ticket::Client client([&a](const okayac::backend::Request& r) {
            const okay::account::ApiResponse x = a.ApiTimed(r.path, r.method, r.body, r.header, 8);
            okayac::ticket::Reply out; out.reached = x.reached; out.status = x.status; out.body = x.body; return out;
        }, opt);
        okayac::ticket::Hooks hk;
        hk.hwHash = [] { return okayac::MachineHash(); };
        hk.signedIn = [&a] { return a.IsLoggedIn() && a.IsOnline(); };
        okayac::ticket::JoinTarget t;
        if (j->link.Relayed()) { t.relayed = true; t.relayCode = j->link.code; }
        else { const std::string h = j->link.addr; const std::size_t c = h.rfind(':'); t.host = c == std::string::npos ? h : h.substr(0, c); t.port = c == std::string::npos ? 0 : (std::uint16_t)std::atoi(h.c_str() + c + 1); }
        const okayac::ticket::Decision d = okayac::ticket::Decide(j->cfg, client, hk, t, "");
        allow = d.allow; banned = d.outcome == okayac::ticket::Outcome::Banned; msg = d.message;
    } catch (...) { allow = j->cfg.mode != okayac::ticket::Mode::Required; if (!allow) msg = "Couldn't check the session ticket."; }
    okay::thr::LockMutex(g_tkMutex);
    g_tkAllow = allow; g_tkBanned = banned; g_tkMsg = msg; g_tkDone = true;
    okay::thr::UnlockMutex(g_tkMutex);
    g_tkBusy = false;
    return 0;
}
// True when the start must wait for (or stop at) the ticket check. Called by MpLaunch; false = go ahead.
bool TicketPreflightHolds(const okay::JoinLink& l, const std::string& scene) {
    if (!g_tkMutex) g_tkMutex = okay::thr::CreateMutex();
    const std::string link = l.Format();
    if (g_tkOkLink == link) { g_tkOkLink.clear(); return false; }            // checked a moment ago
    okayac::ticket::Config cfg = TicketConfigFor(scene);
    if (cfg.mode == okayac::ticket::Mode::Off || g_pollCfgDir.empty()) return false;
    const long now = (long)std::time(nullptr);
    for (const auto& b : g_acMyBans)                                         // already known: no need to ask
        if (b.until == 0 || b.until > now) {
            Toast("You can't join: this account has an anti-cheat ban (see the banner at the top).", 2);
            return true;
        }
    if (g_tkBusy) { Toast("One moment\xE2\x80\xA6", 0); return true; }
    g_tkBusy = true;
    okay::thr::LockMutex(g_tkMutex); g_tkLink = link; g_tkDone = false; okay::thr::UnlockMutex(g_tkMutex);
    TkJob* job = new TkJob{cfg, l};
    Toast("Checking your session\xE2\x80\xA6", 0);
    if (okay::thr::Thread* t = okay::thr::CreateThread(TicketPreflightWorker, "okay-ticket", job)) okay::thr::DetachThread(t);
    else { g_tkBusy = false; delete job; return cfg.mode == okayac::ticket::Mode::Required; }
    return true;
}
// Every frame: act on the worker's answer.
void PumpTicketPreflight() {
    if (!g_tkMutex) return;
    bool done = false, allow = false, banned = false; std::string msg, link;
    okay::thr::LockMutex(g_tkMutex);
    if (g_tkDone) { done = true; allow = g_tkAllow; banned = g_tkBanned; msg = g_tkMsg; link = g_tkLink; g_tkDone = false; }
    okay::thr::UnlockMutex(g_tkMutex);
    if (!done) return;
    if (banned) {
        AcMeQueue(new AcMeJob);   // refresh ac_my_bans: the banner at the top of every page shows the reason and the appeal
        Toast("You can't join: this account or machine has an anti-cheat ban. Details are at the top of the page.", 2);
        return;
    }
    if (!allow) { Toast(msg.empty() ? std::string("Couldn't get a session ticket, so the game was not started.") : msg, 2); return; }
    okay::JoinLink l;
    if (okay::JoinLink::Parse(link, l)) { g_tkOkLink = link; MpLaunch(l); }
}

// Banner + appeal for my active anti-cheat ban, at the top of every page.
void DrawAcBanBanner() {
    const long now = (long)std::time(nullptr);
    const acb::MyBan* ban = nullptr;
    for (const auto& b : g_acMyBans) if (b.until == 0 || b.until > now) { ban = &b; break; }
    static bool appealOpen = false;
    static char appealBuf[2000] = "";
    if (ban) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = 54.0f;
        const std::string head = "Anti-cheat ban, " + AcUntil(ban->until) + ": " + ban->reason;
        std::string sub = ban->appealStatus == "open" ? "Your appeal is waiting for a decision."
                        : ban->appealStatus == "rejected" ? "Your appeal was rejected" + (ban->appealNote.empty() ? std::string(".") : ": " + ban->appealNote)
                        : "Think this is a mistake? You can appeal.";
        d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), okaytheme::DangerFillU32(0.922f), 10.0f);
        d->AddText(ImVec2(p.x + 14, p.y + 8), IM_COL32_WHITE, head.c_str());
        d->AddText(ImVec2(p.x + 14, p.y + 28), okaytheme::WarningU32(), sub.c_str());
        if (ban->CanAppeal()) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + w - 130, p.y + 12));
            if (ImGui::Button("Appeal\xE2\x80\xA6", ImVec2(116, 30))) { appealOpen = true; appealBuf[0] = '\0'; }
        }
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(w, h + 8));
    }
    static bool demoOnce = false;
    if (!demoOnce && ban && std::getenv("OKAY_UI_DEMO_ACAPPEAL")) {   // (env: dev screenshot hook)
        demoOnce = true; appealOpen = true;
        std::snprintf(appealBuf, sizeof(appealBuf), "I don't run trainers. The DLL is my keyboard's macro software (it hooks every game). "
                                                    "I've uninstalled it; happy to send a replay.");
    }
    if (appealOpen) { ImGui::OpenPopup("Appeal your ban"); appealOpen = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Appeal your ban", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (ban) {
            ImGui::TextWrapped("Ban #%ld: %s (%s)", ban->id, ban->reason.c_str(), AcUntil(ban->until).c_str());
            ImGui::TextDisabled("An admin reads this. Say what happened; %d appeal%s left for this ban.", ban->appealsLeft, ban->appealsLeft == 1 ? "" : "s");
            ImGui::InputTextMultiline("##acappeal", appealBuf, sizeof(appealBuf), ImVec2(460, 120));
            acb::Request q; std::string err;
            if (PrimaryButton("Send appeal", ImVec2(140, 30))) {
                if (std::getenv("OKAY_UI_DEMO_USER")) { Toast("Demo: nothing is sent", 0); ImGui::CloseCurrentPopup(); }
                else if (!acb::FileAppeal(ban->id, appealBuf, okayac::MachineHash(), q, &err)) Toast(err, 2);
                else { AcMeJob* j = new AcMeJob; j->hasAction = true; j->action = q; j->okMsg = "Appeal sent"; AcMeQueue(j); ImGui::CloseCurrentPopup(); }
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel", ImVec2(110, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace okl
