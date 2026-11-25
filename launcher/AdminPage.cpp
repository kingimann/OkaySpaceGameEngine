// Reports and the Admin page (AdminPage.hpp): the "Report ..." dialog for players, messages, posts, listings and
// servers; Admin > Reports; the Admin page itself; and the banner for a banned player. Moved out of launcher/main.cpp
// unchanged (docs/engine/modularization_plan.md, Part C section 20, batch L5).
#include "LauncherShared.hpp"
#include "NavIcons.hpp"   // SlateCombo, the launcher's dropdown
#include "AdminPage.hpp"
#include "AntiCheatPages.hpp"   // acb, AcMeJob, DrawAntiCheatAdmin
#include "UserReports.hpp"      // okayrep: report requests and outcomes
#include "CoverArtAvatars.hpp"  // DrawAvatar
#include "OkayTheme.hpp"
#include "okay/Platform/Admin.hpp"
#include "okay/Platform/Forums.hpp"
#include "okay/Platform/Workshop.hpp"
#include "okay/Platform/Account/AccountService.hpp"

namespace okl {

struct AdminJob { int kind = 0; std::string s1, s2, s3; long n1 = 0, n2 = 0; };
// kind: 1 overview, 2 find players(s1), 3 role(uid s1, badge s2, on n1; reload s3),
//       4 verify(uid s1, on n1; s3), 5 ban(uid s1, reason s2, hours n1; s3), 6 unban(uid s1; s3),
//       7 market(status s1), 8 market action(id n1, act s1, note s2; status s3),
//       9 sections, 10 section staff(id n1), 11 section role(id n1, uid s1, role|"remove" s2),
//       12 delete section(n1), 13 threads, 14 thread action(id n1, act s1), 15 bans, 16 log
struct AdminData {
    okay::AdminStats stats;
    std::vector<okay::AdminLogEntry> log;
    std::vector<okay::AdminUser> users;
    std::vector<okay::WorkshopItem> items;
    std::vector<okay::ForumSection> sections; bool sectionsSup = true;
    std::vector<okay::ForumMember> members; long membersOf = 0;
    std::vector<okay::ForumThread> threads;
    std::vector<okay::AdminBan> bans;
};
static okay::thr::Mutex* g_adMutex = nullptr;
static volatile bool g_adBusy = false;
static AdminData g_adNew; static unsigned g_adNewMask = 0;   // (guarded) which parts arrived
static std::string g_adMsg; static bool g_adMsgErr = false;  // (guarded)
enum : unsigned { kAdStats = 1, kAdLog = 2, kAdUsers = 4, kAdItems = 8, kAdSections = 16, kAdMembers = 32, kAdThreads = 64, kAdBans = 128 };
static int AdminWorker(void* p) {
    std::unique_ptr<AdminJob> j(static_cast<AdminJob*>(p));
    AdminData d; unsigned mask = 0; std::string msg; bool err = false;
    auto say = [&](bool ok, const char* good, const char* bad) { msg = ok ? good : bad; err = !ok; };
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Admin ad(a);
        okay::Workshop ws(a);
        okay::Forums fo(a);
        switch (j->kind) {
        case 1: d.stats = ad.Stats(); d.log = ad.Log(12); mask = kAdStats | kAdLog; break;
        case 3: say(ad.SetBadge(j->s1, j->s2, j->n1 != 0), j->n1 ? "Role given" : "Role removed", "You can't change that role"); break;
        case 4: say(ad.SetVerified(j->s1, j->n1 != 0), j->n1 ? "Verified" : "Verification removed", "Only staff can verify"); break;
        case 5: say(ad.Ban(j->s1, j->s2, j->n1), "Player banned", "You can't ban that player"); break;
        case 6: say(ad.Unban(j->s1), "Ban lifted", "Couldn't lift the ban"); break;
        case 8: {
            const long id = j->n1; const std::string& act = j->s1;
            bool ok = false;
            if (act == "approve")        ok = ws.SetStatus(id, "approved", j->s2);
            else if (act == "reject")    ok = ws.SetStatus(id, "rejected", j->s2);
            else if (act == "requeue")   ok = ws.SetStatus(id, "pending", j->s2);
            else if (act == "feature")   ok = ws.SetFeatured(id, true);
            else if (act == "unfeature") ok = ws.SetFeatured(id, false);
            else if (act == "lock")      ok = ws.SetReviewsLocked(id, true);
            else if (act == "unlock")    ok = ws.SetReviewsLocked(id, false);
            else if (act == "delete")    ok = ws.Remove(id);
            say(ok, act == "approve" ? "Approved \xE2\x80\x94 it's live" : act == "reject" ? "Rejected" : act == "delete" ? "Listing deleted"
                    : act == "lock" ? "Reviews turned off" : act == "unlock" ? "Reviews turned on" : "Updated",
                "That didn't go through (permissions?)");
            break;
        }
        case 11: say(j->s2 == "remove" ? fo.RemoveMember(j->n1, j->s1) : fo.SetRole(j->n1, j->s1, j->s2),
                     j->s2 == "remove" ? "Removed from the section" : "Role changed", "You can't change that member"); break;
        case 12: say(fo.DeleteSection(j->n1), "Section deleted", "Only staff can delete sections"); break;
        case 14: {
            const std::string& act = j->s1; bool ok = false;
            if (act == "delete") ok = fo.DeleteThread(j->n1);
            else if (act == "lock" || act == "unlock") ok = fo.SetLocked(j->n1, act == "lock");
            else if (act == "pin" || act == "unpin") ok = fo.SetPinned(j->n1, act == "pin");
            say(ok, act == "delete" ? "Post removed" : act == "lock" ? "Comments turned off" : act == "unlock" ? "Comments turned on" : "Updated",
                "That didn't go through (permissions?)");
            break;
        }
        default: break;
        }
        // (Re)load whatever the job touches.
        if (j->kind == 2 || (j->kind >= 3 && j->kind <= 6)) { d.users = ad.FindUsers(j->kind == 2 ? j->s1 : j->s3); mask |= kAdUsers; }
        if (j->kind == 5 || j->kind == 6 || j->kind == 15) { d.bans = ad.Bans(); mask |= kAdBans; }
        if (j->kind == 7 || j->kind == 8) { d.items = ws.ByStatus(j->kind == 7 ? j->s1 : j->s3); mask |= kAdItems; }
        if (j->kind == 9 || j->kind == 12) { d.sections = fo.Sections(&d.sectionsSup); mask |= kAdSections; }
        if (j->kind == 10 || j->kind == 11) { d.members = fo.Members(j->n1); d.membersOf = j->n1; mask |= kAdMembers; }
        if (j->kind == 13 || j->kind == 14 || j->kind == 12) { d.threads = fo.Threads(); mask |= kAdThreads; }
        if (j->kind == 16) { d.log = ad.Log(200); mask |= kAdLog; }
    } catch (...) { msg = "Something went wrong"; err = true; }
    okay::thr::LockMutex(g_adMutex);
    if (mask & kAdStats) g_adNew.stats = d.stats;
    if (mask & kAdLog) g_adNew.log = std::move(d.log);
    if (mask & kAdUsers) g_adNew.users = std::move(d.users);
    if (mask & kAdItems) g_adNew.items = std::move(d.items);
    if (mask & kAdSections) { g_adNew.sections = std::move(d.sections); g_adNew.sectionsSup = d.sectionsSup; }
    if (mask & kAdMembers) { g_adNew.members = std::move(d.members); g_adNew.membersOf = d.membersOf; }
    if (mask & kAdThreads) g_adNew.threads = std::move(d.threads);
    if (mask & kAdBans) g_adNew.bans = std::move(d.bans);
    g_adNewMask |= mask;
    if (!msg.empty()) { g_adMsg = msg; g_adMsgErr = err; }
    okay::thr::UnlockMutex(g_adMutex);
    g_adBusy = false;
    return 0;
}

static AdminData DemoAdminData() {
    AdminData d;
    const long t0 = (long)std::time(nullptr), h = 3600, day = 86400;
    d.stats.ok = true; d.stats.players = 1284; d.stats.newWeek = 87; d.stats.onlineNow = 46; d.stats.banned = 3;
    d.stats.staff = 6; d.stats.threadsWeek = 142; d.stats.items = 318; d.stats.pending = 4;
    auto lg = [&](const char* a, const char* act, const char* t, const char* det, long ago) {
        okay::AdminLogEntry e; e.actor = a; e.action = act; e.target = t; e.detail = det; e.when = t0 - ago; d.log.push_back(e); };
    lg("nova", "approve", "maya", "Skyline Sprint", 12 * 60); lg("nova", "ban", "spambot42", "Spamming links (permanent)", 2 * h);
    lg("riley", "role_add", "priya", "moderator", 5 * h); lg("nova", "verify", "pixelforge", "", day);
    lg("riley", "reject", "quickcash", "Asset flip: ripped models", 2 * day); lg("nova", "unban", "leo", "", 3 * day);
    auto us = [&](const char* id, const char* u, const char* dn, std::vector<std::string> b, bool v, bool on, long seen, long joined, bool banned = false, const char* why = "", long until = 0) {
        okay::AdminUser x; x.id = id; x.username = u; x.display = dn; x.badges = b; x.verified = v; x.online = on;
        x.lastSeen = t0 - seen; x.joined = t0 - joined; x.banned = banned; x.banReason = why; x.banUntil = until; d.users.push_back(x); };
    us("u-nova", "nova", "Nova", {"admin"}, true, true, 0, 300 * day);
    us("u-riley", "riley", "Riley", {"staff"}, true, true, 0, 280 * day);
    us("u-priya", "priya", "Priya Natarajan", {"moderator", "developer"}, false, false, 3 * h, 200 * day);
    us("u-maya", "maya", "Maya Chen", {"beta"}, true, true, 0, 290 * day);
    us("u-pixel", "pixelforge", "PixelForge Studio", {"partner"}, true, false, day, 120 * day);
    us("u-spam", "spambot42", "spambot42", {}, false, false, 2 * h, 1 * day, true, "Spamming links", 0);
    us("u-leo", "leo", "Leo Martins", {}, false, false, 5 * day, 90 * day);
    auto it = [&](long id, const char* t, const char* a, const char* k, const char* desc, const char* st, long ago, const char* note = "") {
        okay::WorkshopItem w; w.id = id; w.title = t; w.author = a; w.kind = k; w.description = desc; w.status = st; w.created = t0 - ago;
        w.reviewNote = note; w.url = "https://demo.supabase.co/storage/v1/object/public/workshop/u/listing.zip"; d.items.push_back(w); };
    it(201, "Neon Racer Kit", "maya", "template", "A starter project for arcade racers: car controller, lap timer, neon track pieces and a ghost replay.", "pending", 40 * 60);
    it(202, "Low-poly Forest Pack", "pixelforge", "model", "60 trees, rocks and props, game-ready, 3 LODs each. CC-BY.", "pending", 3 * h);
    it(203, "Dungeon Crawler Demo", "leo", "game", "Three floors, two bosses, controller support. Built in OkaySpace.", "pending", 9 * h);
    it(204, "Free V-Bucks Generator", "spambot42", "tool", "Totally legit.", "pending", 20 * h);
    okay::ForumSection s1; s1.id = 1; s1.slug = "gamedev"; s1.name = "Game Dev"; s1.members = 412; s1.threads = 96; s1.color = 2; d.sections.push_back(s1);
    okay::ForumSection s2; s2.id = 2; s2.slug = "showcase"; s2.name = "Showcase"; s2.members = 230; s2.threads = 58; s2.color = 5; d.sections.push_back(s2);
    okay::ForumSection s3; s3.id = 3; s3.slug = "help"; s3.name = "Help & Support"; s3.members = 518; s3.threads = 211; s3.color = 7; d.sections.push_back(s3);
    auto mb = [&](const char* uid, const char* n, const char* r) { okay::ForumMember m; m.uid = uid; m.name = n; m.role = r; d.members.push_back(m); };
    mb("u-maya", "maya", "admin"); mb("u-priya", "priya", "moderator"); mb("u-leo", "leo", "moderator"); d.membersOf = 1;
    auto th = [&](long id, const char* t, const char* a, long sec, long ago, bool locked = false, bool pinned = false) {
        okay::ForumThread x; x.id = id; x.title = t; x.author = a; x.sectionId = sec; x.when = t0 - ago; x.locked = locked; x.pinned = pinned; d.threads.push_back(x); };
    th(901, "Weekly jam: theme is \"Tiny worlds\"", "maya", 1, 2 * h, false, true);
    th(902, "Best way to do footstep sounds?", "leo", 3, 4 * h);
    th(903, "CHEAP GAME KEYS click here", "spambot42", 2, 5 * h);
    th(904, "Show your skyboxes", "priya", 2, day, true);
    okay::AdminBan b; b.userId = "u-spam"; b.username = "spambot42"; b.reason = "Spamming links"; b.when = t0 - 2 * h; d.bans.push_back(b);
    okay::AdminBan b2; b2.userId = "u-x"; b2.username = "toxic_tim"; b2.reason = "Harassment in DMs"; b2.until = t0 + 5 * day; b2.when = t0 - 2 * day; d.bans.push_back(b2);
    return d;
}
// ---- Reports: abuse, spam, scams, illegal content (docs/sql/reports.sql) -------
// Every "Report ..." control (a player, a chat message, a forum post or comment, a
// marketplace listing, a server) calls OpenReport; the dialog is drawn once per
// frame (DrawReportPopup). "Report sent" is only shown after the server accepted
// the row; a missing table, a signed-out player, a duplicate and a flood limit
// each get their own sentence (shared/UserReports.hpp). Moderators review under
// Admin > Reports (DrawReportsAdmin, below).
namespace rep = okayrep;
static okay::thr::Mutex* g_repMutex = nullptr;
static rep::Target g_repTarget = rep::Target::User;
static std::string g_repId, g_repLabel;
static bool g_repOpenReq = false;
static volatile bool g_repBusy = false;
static bool g_repResultReady = false; static rep::Outcome g_repResult = rep::Outcome::Other;   // (guarded)
struct RepSendJob { rep::Request req; };
static int ReportSendWorker(void* p) {
    std::unique_ptr<RepSendJob> j(static_cast<RepSendJob*>(p));
    rep::Outcome o = rep::Outcome::NoNetwork;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        if (!a.IsLoggedIn()) o = rep::Outcome::SignedOut;
        else { const auto r = acb::Send(a, j->req); o = r.ok ? rep::Outcome::Sent : rep::Classify(r.status, r.body); }
    } catch (...) { o = rep::Outcome::NoNetwork; }
    if (!g_repMutex) g_repMutex = okay::thr::CreateMutex();
    okay::thr::LockMutex(g_repMutex);
    g_repResult = o; g_repResultReady = true;
    okay::thr::UnlockMutex(g_repMutex);
    g_repBusy = false;
    return 0;
}
void OpenReport(rep::Target t, const std::string& id, const std::string& label) {
    g_repTarget = t; g_repId = id; g_repLabel = label; g_repOpenReq = true;
}
void DrawReportPopup() {
    static const bool kDemo = std::getenv("OKAY_UI_DEMO_USER") != nullptr;
    static int reason = -1;
    static char note[rep::kNoteMax + 8] = "";
    static std::string inlineMsg;     // why the last send did not go through ("" = none)
    static bool closeNow = false;
    if (!g_repMutex) g_repMutex = okay::thr::CreateMutex();
    if (g_repOpenReq) {
        g_repOpenReq = false; reason = -1; note[0] = '\0'; inlineMsg.clear(); closeNow = false;
        ImGui::OpenPopup("Report##userreport");
    }
    // The server's answer (also when the dialog was closed while it was out).
    bool ready = false; rep::Outcome res = rep::Outcome::Other;
    okay::thr::LockMutex(g_repMutex);
    if (g_repResultReady) { g_repResultReady = false; ready = true; res = g_repResult; }
    okay::thr::UnlockMutex(g_repMutex);
    if (ready) {
        if (rep::Accepted(res)) { Toast(rep::Message(res), 1); closeNow = true; }
        else { inlineMsg = rep::Message(res); Toast(inlineMsg, 2); }
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Report##userreport", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (closeNow) { closeNow = false; ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        ImGui::Text("Report %s", g_repLabel.empty() ? rep::TargetLabel(rep::TargetKey(g_repTarget)) : g_repLabel.c_str());
        ImGui::TextDisabled("Moderators review every report. Reporting is not anonymous to them; the other person is not told.");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextUnformatted("Why are you reporting this?");
        int n = 0; const rep::Reason* rs = rep::Reasons(n);
        for (int i = 0; i < n; ++i) if (ImGui::RadioButton(rs[i].label, reason == i)) reason = i;
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::InputTextMultiline("##reportnote", note, sizeof(note), ImVec2(420, 70));
        ImGui::TextDisabled("Optional note (what happened, where). %d / %d", (int)std::strlen(note), (int)rep::kNoteMax);
        if (std::strlen(note) > rep::kNoteMax) note[rep::kNoteMax] = '\0';
        if (!inlineMsg.empty()) {
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.5f, 0.45f, 1));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420);
            ImGui::TextUnformatted(inlineMsg.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0, 4));
        const bool busy = g_repBusy;
        if (busy) ImGui::BeginDisabled();
        if (PrimaryButton(busy ? "Sending\xE2\x80\xA6" : "Send report", ImVec2(140, 30))) {
            rep::Request q; std::string err;
            inlineMsg.clear();
            if (reason < 0) inlineMsg = "Pick a reason first.";
            else if (!rep::Build(g_repTarget, g_repId, rs[reason].key, note, std::string(), q, &err)) inlineMsg = err;
            else if (kDemo) { Toast("Demo: nothing is sent", 0); ImGui::CloseCurrentPopup(); }
            else if (g_pollCfgDir.empty()) inlineMsg = rep::Message(rep::Outcome::SignedOut);
            else {
                g_repBusy = true;
                RepSendJob* j = new RepSendJob; j->req = q;
                if (okay::thr::Thread* t = okay::thr::CreateThread(ReportSendWorker, "okay-report", j)) okay::thr::DetachThread(t);
                else { g_repBusy = false; delete j; inlineMsg = rep::Message(rep::Outcome::NoNetwork); }
            }
        }
        if (busy) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// Admin > Reports (every moderator): the open reports with what was reported, why and by whom;
// mark each resolved or dismissed (or reopen it). The server decides who may (RLS in reports.sql).
struct RepAdminData {
    std::vector<rep::Row> rows;
    std::map<std::string, std::string> names;   // uuid -> username
    std::string loadError;
};
struct RepAdminJob { bool hasAction = false; rep::Request action; std::string okMsg, status; };
static okay::thr::Mutex* g_repAdMutex = nullptr;
static volatile bool g_repAdBusy = false;
static RepAdminData g_repAdNew; static bool g_repAdReady = false;   // (guarded)
static std::string g_repAdMsg; static bool g_repAdMsgErr = false;   // (guarded)
static int RepAdminWorker(void* p) {
    std::unique_ptr<RepAdminJob> j(static_cast<RepAdminJob*>(p));
    RepAdminData d; std::string msg; bool err = false;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        if (j->hasAction) {
            const auto r = acb::Send(a, j->action);
            msg = r.ok ? j->okMsg : rep::AdminError(r.status, r.body); err = !r.ok;
        }
        const auto r = acb::Send(a, rep::List(j->status, 200));
        if (!r.ok || !rep::ParseRows(r.body, d.rows)) d.loadError = rep::AdminError(r.status, r.body);
        std::vector<std::string> ids;
        for (const auto& x : d.rows) { ids.push_back(x.reporter); ids.push_back(x.targetUser); }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        for (std::size_t i = 0; i < ids.size(); i += 80) {
            std::vector<std::string> part(ids.begin() + (long)i, ids.begin() + (long)std::min(ids.size(), i + 80));
            const auto nr = acb::Send(a, acb::Usernames(part));
            if (nr.ok) acb::ParseUsernames(nr.body, d.names);
        }
    } catch (...) { msg = "Something went wrong"; err = true; }
    okay::thr::LockMutex(g_repAdMutex);
    g_repAdNew = std::move(d); g_repAdReady = true;
    if (!msg.empty()) { g_repAdMsg = msg; g_repAdMsgErr = err; }
    okay::thr::UnlockMutex(g_repAdMutex);
    g_repAdBusy = false;
    return 0;
}
static RepAdminData DemoRepAdminData() {
    RepAdminData d;
    const long t0 = (long)std::time(nullptr);
    const std::string uA = "0b5a4c1e-0000-4000-8000-000000000011", uB = "0b5a4c1e-0000-4000-8000-000000000012", uC = "0b5a4c1e-0000-4000-8000-000000000013";
    d.names[uA] = "maya"; d.names[uB] = "tim"; d.names[uC] = "spambot77";
    auto row = [&](long id, const char* type, const char* tid, const std::string& tu, const char* why, const char* note, const char* snap, const char* st, const std::string& by, long ago) {
        rep::Row r; r.id = id; r.targetType = type; r.targetId = tid; r.targetUser = tu; r.reason = why; r.note = note; r.snapshot = snap;
        r.status = st; r.reporter = by; r.created = t0 - ago; d.rows.push_back(r); };
    row(3, "forum_post", "41", uC, "spam", "Same link in five threads", "FREE ROBUX click here http://totally.legit", "open", uA, 600);
    row(2, "message", "812", uC, "scam", "", "send me your password and I will give you coins", "open", uB, 5400);
    row(1, "market_item", "7", uB, "illegal", "Uploads a cracked paid asset pack", "Neon UI Kit (cracked)", "resolved", uA, 86400);
    return d;
}
static void DrawReportsAdmin() {
    static const bool kDemo = std::getenv("OKAY_UI_DEMO_USER") != nullptr;
    static RepAdminData D;
    static bool loaded = false, firstFrame = true;
    static int filter = 0;   // Open, Resolved, Dismissed, All
    static long selReport = 0;
    static char note[300] = "";
    static const char* kStatus[] = {"open", "resolved", "dismissed", ""};
    const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text), cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    if (!g_repAdMutex) g_repAdMutex = okay::thr::CreateMutex();
    if (kDemo && !loaded) { loaded = true; D = DemoRepAdminData(); }
    auto queue = [&](RepAdminJob* j) {
        if (kDemo) { delete j; Toast("Demo: nothing is sent", 0); return; }
        if (g_repAdBusy) { delete j; Toast("Still working on the last action\xE2\x80\xA6", 0); return; }
        j->status = kStatus[filter];
        g_repAdBusy = true;
        if (okay::thr::Thread* t = okay::thr::CreateThread(RepAdminWorker, "okay-repadmin", j)) okay::thr::DetachThread(t);
        else { g_repAdBusy = false; delete j; }
    };
    if (!kDemo) {
        if (firstFrame) { firstFrame = false; queue(new RepAdminJob); }
        okay::thr::LockMutex(g_repAdMutex);
        if (g_repAdReady) { g_repAdReady = false; D = g_repAdNew; loaded = true; }
        if (!g_repAdMsg.empty()) { Toast(g_repAdMsg, g_repAdMsgErr ? 2 : 1); g_repAdMsg.clear(); }
        okay::thr::UnlockMutex(g_repAdMutex);
    }
    auto name = [&](const std::string& uid) -> std::string {
        if (uid.empty()) return std::string("(gone)");
        auto it = D.names.find(uid);
        return it != D.names.end() && !it->second.empty() ? "@" + it->second : uid.substr(0, 8);
    };
    auto statusCol = [](const std::string& s) -> ImU32 {
        return s == "open" ? IM_COL32(236, 150, 60, 255) : s == "resolved" ? IM_COL32(67, 181, 129, 255) : IM_COL32(120, 128, 140, 255);
    };
    ImGui::SetNextItemWidth(150);
    if (SlateCombo("##repfilter", &filter, "Open\0Resolved\0Dismissed\0All\0")) { selReport = 0; queue(new RepAdminJob); }
    ImGui::SameLine();
    if (ImGui::Button(g_repAdBusy ? "Loading\xE2\x80\xA6" : "Refresh", ImVec2(110, 28))) queue(new RepAdminJob);
    ImGui::SameLine();
    ImGui::TextDisabled("Reports from players about people, messages, posts, listings and servers. Cheating reports are under Anti-cheat.");
    ImGui::Dummy(ImVec2(0, 4));
    if (!D.loadError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.45f, 1));
        ImGui::TextWrapped("%s", D.loadError.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
    ImGui::BeginChild("##repcard", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    int shown = 0;
    for (const auto& r : D.rows) {
        if (filter < 3 && r.status != kStatus[filter]) continue;
        ++shown;
        ImGui::PushID((int)r.id);
        const bool sel = selReport == r.id;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##rep", sel, 0, ImVec2(0, 44))) selReport = sel ? 0 : r.id;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ImVec2(p.x + 6, p.y + 4), cDim, RelTime(r.created).c_str());
        const std::string head = std::string(rep::TargetLabel(r.targetType)) + ": " + rep::ReasonLabel(r.reason) +
                                 (r.targetType == "user" ? std::string() : "  \xC2\xB7  by " + name(r.targetUser));
        dl->AddText(ImVec2(p.x + 96, p.y + 4), cText, head.c_str());
        std::string what = r.snapshot.empty() ? (r.targetType == "user" ? name(r.targetId) : "#" + r.targetId) : r.snapshot;
        for (auto& c : what) if (c == '\n' || c == '\r') c = ' ';
        dl->AddText(ImVec2(p.x + 96, p.y + 23), cDim, FitText(what, std::max(120.0f, ImGui::GetContentRegionAvail().x - 260)).c_str());
        const std::string tag = "reported by " + name(r.reporter);
        const ImVec2 ts = ImGui::CalcTextSize(r.status.c_str());
        const float rx = p.x + ImGui::GetContentRegionAvail().x;
        dl->AddRectFilled(ImVec2(rx - ts.x - 18, p.y + 4), ImVec2(rx - 4, p.y + 8 + ts.y), statusCol(r.status), 8.0f);
        dl->AddText(ImVec2(rx - ts.x - 11, p.y + 6), IM_COL32_WHITE, r.status.c_str());
        dl->AddText(ImVec2(rx - ImGui::CalcTextSize(tag.c_str()).x - 6, p.y + 24), cDim, tag.c_str());
        if (sel) {
            ImGui::Indent(96);
            ImGui::PushTextWrapPos(0.0f);
            if (!r.snapshot.empty()) ImGui::TextWrapped("\"%s\"", r.snapshot.c_str());
            if (!r.note.empty()) ImGui::TextDisabled("Reporter's note: %s", r.note.c_str());
            if (r.targetType != "user" || !r.targetUser.empty()) ImGui::TextDisabled("Belongs to %s  \xC2\xB7  %s #%s", name(r.targetUser).c_str(), r.targetType.c_str(), r.targetId.c_str());
            if (!r.reviewNote.empty()) ImGui::TextDisabled("Review note: %s", r.reviewNote.c_str());
            ImGui::PopTextWrapPos();
            if (r.targetType == "user" && ImGui::SmallButton("Open profile")) OpenPublicProfile(r.targetId, std::string(), std::string());
            ImGui::SetNextItemWidth(260);
            ImGui::InputTextWithHint("##repnote", "Note (optional)", note, sizeof(note));
            ImGui::SameLine();
            rep::Request q; std::string err;
            auto set = [&](const char* st, const char* okMsg) {
                if (rep::SetStatus(r.id, st, note, q, &err)) {
                    RepAdminJob* j = new RepAdminJob; j->hasAction = true; j->action = q; j->okMsg = okMsg; queue(j); note[0] = '\0';
                } else Toast(err, 2);
            };
            if (r.status != "resolved" && PrimaryButton("Resolve", ImVec2(90, 0))) set("resolved", "Marked resolved");
            ImGui::SameLine();
            if (r.status != "dismissed" && ImGui::Button("Dismiss")) set("dismissed", "Report dismissed");
            ImGui::SameLine();
            if (r.status != "open" && ImGui::Button("Reopen")) set("open", "Report reopened");
            ImGui::Unindent(96);
            ImGui::Dummy(ImVec2(0, 4));
        }
        ImGui::PopID();
    }
    if (!shown) ImGui::TextDisabled(!D.loadError.empty() ? "" : loaded ? "Nothing here." : "Loading\xE2\x80\xA6");
    ImGui::EndChild(); ImGui::PopStyleVar(2); ImGui::PopStyleColor();
}

// "Report cheating" (a chat's menu). Drawn from the chat window itself, so the
// dialog sits above the chat rather than under it.
void DrawAcReportPopup() {
    static char repWhy[300] = "", repGame[120] = "", repDetails[1000] = "";
    if (g_acReportOpen) { ImGui::OpenPopup("Report cheating"); g_acReportOpen = false; repWhy[0] = repGame[0] = repDetails[0] = '\0'; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Report cheating", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("Report %s to the OkaySpace admins", g_acReportName.c_str());
        ImGui::TextDisabled("Admins review every report. Say what you saw; a match time or replay helps.");
        ImGui::SetNextItemWidth(420); ImGui::InputTextWithHint("##acrwhy", "What they did (aimbot, speedhack, ...)", repWhy, sizeof(repWhy));
        ImGui::SetNextItemWidth(420); ImGui::InputTextWithHint("##acrgame", "Game (optional)", repGame, sizeof(repGame));
        ImGui::InputTextMultiline("##acrdet", repDetails, sizeof(repDetails), ImVec2(420, 80));
        acb::Request q; std::string err;
        if (PrimaryButton("Send report", ImVec2(140, 30))) {
            if (std::getenv("OKAY_UI_DEMO_USER")) { Toast("Demo: nothing is sent", 0); ImGui::CloseCurrentPopup(); }
            else if (!acb::FileReport(g_acReportUid, repGame, repWhy, repDetails, std::string(), q, &err)) Toast(err, 2);
            else { AcMeJob* j = new AcMeJob; j->hasAction = true; j->action = q; j->okMsg = "Report sent. Thanks!"; AcMeQueue(j); ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void DrawReportsAdmin();   // Admin > Reports (above)

void DrawAdminPage(okay::account::AccountService& account) {
    static const bool kDemo = std::getenv("OKAY_UI_DEMO_USER") != nullptr;
    static int sub = std::getenv("OKAY_UI_DEMO_ADSUB") ? std::atoi(std::getenv("OKAY_UI_DEMO_ADSUB")) : 0;   // (env: dev screenshot hook)
    static AdminData D;
    static bool seeded = false;
    static unsigned loadedMask = 0;
    static char userQ[48] = "";
    static int selUser = -1;
    static char banReason[200] = "";
    static int banDur = 2;
    static int mktView = 0;                 // 0 pending, 1 approved, 2 rejected
    static long rejectId = 0; static char rejectNote[200] = "";
    static long selSection = 0;             // 0 = all posts
    const int myRank = g_myRank;
    const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text), cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    if (!g_adMutex) g_adMutex = okay::thr::CreateMutex();
    if (kDemo && !seeded) { seeded = true; D = DemoAdminData(); loadedMask = ~0u; }

    auto queue = [&](AdminJob* j) {
        if (kDemo) { delete j; return; }
        if (g_adBusy) { delete j; Toast("Still working on the last action\xE2\x80\xA6", 0); return; }
        g_adBusy = true;
        if (okay::thr::Thread* t = okay::thr::CreateThread(AdminWorker, "okay-admin", j)) okay::thr::DetachThread(t);
        else { g_adBusy = false; delete j; }
    };
    auto job = [&](int kind) { AdminJob* j = new AdminJob; j->kind = kind; return j; };
    // Pick up results.
    if (!kDemo) {
        okay::thr::LockMutex(g_adMutex);
        const unsigned m = g_adNewMask; g_adNewMask = 0;
        if (m & kAdStats) D.stats = g_adNew.stats;
        if (m & kAdLog) D.log = g_adNew.log;
        if (m & kAdUsers) { D.users = g_adNew.users; if (selUser >= (int)D.users.size()) selUser = -1; }
        if (m & kAdItems) D.items = g_adNew.items;
        if (m & kAdSections) { D.sections = g_adNew.sections; D.sectionsSup = g_adNew.sectionsSup; }
        if (m & kAdMembers) { D.members = g_adNew.members; D.membersOf = g_adNew.membersOf; }
        if (m & kAdThreads) D.threads = g_adNew.threads;
        if (m & kAdBans) D.bans = g_adNew.bans;
        loadedMask |= m;
        if (!g_adMsg.empty()) { Toast(g_adMsg, g_adMsgErr ? 2 : 1); g_adMsg.clear(); }
        okay::thr::UnlockMutex(g_adMutex);
    }

    // ---- Header ----
    {
        ImGui::SetWindowFontScale(1.45f);
        ImGui::TextUnformatted("Admin");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine(0, 12);
        const char* rn = okay::Admin::RankName(myRank);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 ts = ImGui::CalcTextSize(rn);
        const ImU32 rc = myRank >= 3 ? IM_COL32(232, 86, 86, 255) : myRank == 2 ? IM_COL32(88, 140, 240, 255) : IM_COL32(67, 181, 129, 255);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 6), ImVec2(p.x + ts.x + 16, p.y + 6 + ts.y + 6), rc, 9.0f);
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 8, p.y + 9), IM_COL32_WHITE, rn);
        ImGui::Dummy(ImVec2(ts.x + 16, ts.y + 12));
        ImGui::TextDisabled("Everything here is enforced by the server, and every action is written to the audit log.");
    }
    if (myRank < 1) {
        ImGui::Dummy(ImVec2(0, 20));
        ImGui::TextWrapped(myRank < 0 ? "Admin tools aren't set up on this server yet (docs/sql/admin.sql), or you're signed out."
                                      : "You need the moderator, staff or admin role to use these tools.");
        return;
    }

    // ---- Sub-tabs ----
    // "Official game" (publish OkaySpace's own game) is for admins only; the
    // server refuses everyone else anyway (docs/sql/official_game_admin.sql).
    static const char* kSubs[] = {"Overview", "Players", "Marketplace", "Forums", "Bans", "Audit log", "Reports", "Official game", "Anti-cheat"};
    const int nSubs = myRank >= 3 ? 9 : 7;   // Reports: every moderator (docs/sql/reports.sql); Official game and Anti-cheat: admins only
    if (sub >= nSubs || sub < 0) sub = 0;
    {
        ImGui::Dummy(ImVec2(0, 8));
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetCursorScreenPos();
        float x = base.x;
        for (int i = 0; i < nSubs; ++i) {
            char lbl[64];
            if (i == 2 && D.stats.pending > 0) std::snprintf(lbl, sizeof(lbl), "%s  %ld", kSubs[i], D.stats.pending);
            else std::snprintf(lbl, sizeof(lbl), "%s", kSubs[i]);
            const ImVec2 ts = ImGui::CalcTextSize(lbl);
            ImGui::SetCursorScreenPos(ImVec2(x, base.y));
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##adsub", ImVec2(ts.x + 24, 34))) sub = i;
            const bool h = ImGui::IsItemHovered(), on = sub == i;
            ImGui::PopID();
            if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (on || h) d->AddRectFilled(ImVec2(x, base.y), ImVec2(x + ts.x + 24, base.y + 34), IM_COL32(255, 255, 255, on ? 14 : 7), 8.0f);
            d->AddText(ImVec2(x + 12, base.y + 17 - ts.y * 0.5f), on ? cText : cDim, lbl);
            if (on) d->AddRectFilled(ImVec2(x + 12, base.y + 31), ImVec2(x + 12 + ts.x, base.y + 34), ImGui::GetColorU32(kAccent), 2.0f);
            x += ts.x + 24 + 6;
        }
        ImGui::SetCursorScreenPos(ImVec2(base.x, base.y + 34));
        ImVec2 lp = ImGui::GetCursorScreenPos();
        d->AddLine(lp, ImVec2(lp.x + ImGui::GetContentRegionAvail().x, lp.y), IM_COL32(255, 255, 255, 18));
        ImGui::Dummy(ImVec2(0, 12));
    }
    // Load each page's data the first time it's shown.
    static int lastSub = -1;
    if (sub != lastSub && !kDemo) {
        lastSub = sub;
        if (sub == 0) queue(job(1));
        else if (sub == 1 && !(loadedMask & kAdUsers)) { AdminJob* j = job(2); j->s1 = userQ; queue(j); }
        else if (sub == 2) { AdminJob* j = job(7); j->s1 = mktView == 0 ? "pending" : mktView == 1 ? "approved" : "rejected"; queue(j); }
        else if (sub == 3) { queue(job(9)); }
        else if (sub == 4) queue(job(15));
        else if (sub == 5) queue(job(16));
    }
    if (sub == 6) { DrawReportsAdmin(); return; }
    if (sub == 7) { DrawOfficialAdmin(account); return; }
    if (sub == 8) { DrawAntiCheatAdmin(); return; }
    if (sub == 3 && !kDemo && (loadedMask & kAdSections) && !(loadedMask & kAdThreads) && !g_adBusy) { loadedMask |= kAdThreads; queue(job(13)); }

    // Shared bits.
    auto card = [&](const char* id, float w, float h) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
        ImGui::BeginChild(id, ImVec2(w, h), h > 0 ? ImGuiChildFlags_AlwaysUseWindowPadding : (ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding));
    };
    auto cardEnd = [&]() { ImGui::EndChild(); ImGui::PopStyleVar(2); ImGui::PopStyleColor(); };
    auto smallCaps = [&](const char* t) { ImGui::SetWindowFontScale(0.80f); ImGui::TextDisabled("%s", t); ImGui::SetWindowFontScale(1.0f); };
    auto pill = [&](ImDrawList* d, ImVec2 p, const char* t, ImU32 bg, ImU32 fg) -> float {
        const ImVec2 ts = ImGui::CalcTextSize(t);
        const float s = 0.78f, w = ts.x * s + 12;
        d->AddRectFilled(p, ImVec2(p.x + w, p.y + ts.y * s + 6), bg, 8.0f);
        d->AddText(ImGui::GetFont(), ImGui::GetFontSize() * s, ImVec2(p.x + 6, p.y + 3), fg, t);
        return w;
    };
    auto badgeCol = [](const std::string& b) -> ImU32 {
        return b == "admin" ? IM_COL32(232, 86, 86, 255) : b == "staff" ? IM_COL32(88, 140, 240, 255) : b == "moderator" ? IM_COL32(67, 181, 129, 255)
             : b == "developer" ? IM_COL32(150, 110, 230, 255) : b == "partner" ? IM_COL32(236, 170, 60, 255) : IM_COL32(120, 128, 140, 255);
    };
    auto actionCol = [](const std::string& a) -> ImU32 {
        return a == "ban" || a == "reject" || a == "role_remove" || a == "unverify" ? IM_COL32(232, 110, 110, 255)
             : a == "unban" || a == "approve" || a == "role_add" || a == "verify" || a == "official_publish" || a == "official_make_live"
                 ? IM_COL32(90, 200, 130, 255) : IM_COL32(170, 176, 190, 255);
    };
    auto logRows = [&](const std::vector<okay::AdminLogEntry>& L, size_t max) {
        if (L.empty()) { ImGui::TextDisabled(g_adBusy ? "Loading\xE2\x80\xA6" : "Nothing logged yet."); return; }
        for (size_t i = 0; i < L.size() && i < max; ++i) {
            const auto& e = L[i];
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const std::string when = RelTime(e.when);
            d->AddText(p, cDim, when.c_str());
            float x = p.x + 90;
            d->AddText(ImVec2(x, p.y), cText, ("@" + e.actor).c_str()); x += ImGui::CalcTextSize(("@" + e.actor).c_str()).x + 8;
            std::string act = e.action; for (auto& c : act) if (c == '_') c = ' ';
            d->AddText(ImVec2(x, p.y), actionCol(e.action), act.c_str()); x += ImGui::CalcTextSize(act.c_str()).x + 8;
            if (!e.target.empty()) { d->AddText(ImVec2(x, p.y), cText, ("@" + e.target).c_str()); x += ImGui::CalcTextSize(("@" + e.target).c_str()).x + 8; }
            if (!e.detail.empty()) d->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, p.y), cDim, e.detail.c_str(), nullptr, 0.0f);
            ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() + 6));
        }
    };
    const float bodyH = ImGui::GetContentRegionAvail().y - 46.0f;

    if (sub == 0) {   // ======================= Overview =======================
        const okay::AdminStats& s = D.stats;
        struct Tile { const char* label; long v; int go; };
        const Tile tiles[] = {{"PLAYERS", s.players, 1}, {"NEW THIS WEEK", s.newWeek, 1}, {"ONLINE NOW", s.onlineNow, 1},
                              {"AWAITING REVIEW", s.pending, 2}, {"BANNED", s.banned, 4}, {"TEAM", s.staff, 1},
                              {"POSTS THIS WEEK", s.threadsWeek, 3}, {"LISTINGS", s.items, 2}};
        const float aw = ImGui::GetContentRegionAvail().x, g = 10.0f;
        const int cols = aw > 900 ? 4 : 2;
        const float tw = (aw - g * (cols - 1)) / cols, th = 74.0f;
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImDrawList* d = ImGui::GetWindowDrawList();
        for (int i = 0; i < 8; ++i) {
            const ImVec2 p(o.x + (i % cols) * (tw + g), o.y + (i / cols) * (th + g));
            ImGui::SetCursorScreenPos(p);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##tile", ImVec2(tw, th))) sub = tiles[i].go;
            const bool h = ImGui::IsItemHovered();
            ImGui::PopID();
            if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            const bool alert = (i == 3 && s.pending > 0);
            d->AddRectFilled(p, ImVec2(p.x + tw, p.y + th), ImGui::GetColorU32(okaytheme::kRaised), 12.0f);
            if (h) d->AddRect(p, ImVec2(p.x + tw, p.y + th), IM_COL32(255, 255, 255, 30), 12.0f);
            char v[32]; if (s.ok || kDemo) std::snprintf(v, sizeof(v), "%ld", tiles[i].v); else std::snprintf(v, sizeof(v), "\xE2\x80\x94");
            d->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.6f, ImVec2(p.x + 16, p.y + 12), alert ? IM_COL32(240, 190, 80, 255) : cText, v);
            d->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.78f, ImVec2(p.x + 16, p.y + 48), cDim, tiles[i].label);
        }
        ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + ((7 / cols) + 1) * (th + g)));
        ImGui::Dummy(ImVec2(0, 4));
        card("##ad_recent", 0, 0);
        smallCaps("RECENT ACTIONS");
        ImGui::Dummy(ImVec2(0, 4));
        logRows(D.log, 12);
        ImGui::Dummy(ImVec2(0, 4));
        if (ImGui::Button("Full audit log", ImVec2(140, 30))) sub = 5;
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(100, 30))) queue(job(1));
        cardEnd();
    } else if (sub == 1) {   // ======================= Players =======================
        const float aw = ImGui::GetContentRegionAvail().x;
        const float listW = std::min(380.0f, aw * 0.42f);
        card("##ad_users", listW, bodyH);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##adq", "Search players by name\xE2\x80\xA6", userQ, sizeof(userQ), ImGuiInputTextFlags_EnterReturnsTrue)) {
            AdminJob* j = job(2); j->s1 = userQ; queue(j); selUser = -1;
        }
        ImGui::TextDisabled(userQ[0] ? "Press Enter to search." : "Newest players first. Press Enter to search.");
        ImGui::Dummy(ImVec2(0, 4));
        if (D.users.empty()) ImGui::TextDisabled(g_adBusy ? "Loading\xE2\x80\xA6" : "No players found.");
        for (int i = 0; i < (int)D.users.size(); ++i) {
            const okay::AdminUser& u = D.users[i];
            ImGui::PushID(i);
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, rowH = 50.0f;
            if (ImGui::InvisibleButton("##urow", ImVec2(w, rowH))) { selUser = i; banReason[0] = '\0'; }
            const bool h = ImGui::IsItemHovered();
            if (selUser == i || h) d->AddRectFilled(rp, ImVec2(rp.x + w, rp.y + rowH), IM_COL32(255, 255, 255, selUser == i ? 16 : 8), 8.0f);
            ImGui::SetCursorScreenPos(ImVec2(rp.x + 6, rp.y + 7));
            DrawAvatar(u.avatar, u.display, 36.0f);
            const float tx = rp.x + 52;
            d->AddText(ImVec2(tx, rp.y + 7), cText, u.display.c_str());
            float px = tx + ImGui::CalcTextSize(u.display.c_str()).x + 6;
            if (u.verified) { DrawVerifiedBadge(d, ImVec2(px + 6, rp.y + 7 + ImGui::GetTextLineHeight() * 0.5f), 6.0f); px += 16; }
            const int rk = okay::Admin::RankOf(u.badges);
            if (rk > 0) px += pill(d, ImVec2(px + 2, rp.y + 8), okay::Admin::RankName(rk), badgeCol(rk >= 3 ? "admin" : rk == 2 ? "staff" : "moderator"), IM_COL32_WHITE) + 4;
            if (u.banned) pill(d, ImVec2(px + 2, rp.y + 8), "BANNED", IM_COL32(200, 60, 60, 255), IM_COL32_WHITE);
            std::string sub2 = "@" + u.username + "  \xC2\xB7  " + (u.online ? std::string("online") : u.lastSeen > 0 ? "seen " + RelTime(u.lastSeen) : std::string("never online"));
            d->AddText(ImVec2(tx, rp.y + 27), cDim, sub2.c_str());
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rowH + 2));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
        cardEnd();
        ImGui::SameLine(0, 14);
        card("##ad_user", 0, bodyH);
        if (selUser < 0 || selUser >= (int)D.users.size()) {
            ImGui::Dummy(ImVec2(0, 30));
            ImGui::TextDisabled("Pick a player to manage their roles, verification and bans.");
        } else {
            okay::AdminUser& u = D.users[selUser];
            const std::string meName = kDemo ? std::string(std::getenv("OKAY_UI_DEMO_USER")) : account.CurrentSession().username;
            const bool self = u.username == meName;
            const int theirRank = okay::Admin::RankOf(u.badges);
            const ImVec2 hp = ImGui::GetCursorScreenPos();
            DrawAvatar(u.avatar, u.display, 56.0f);
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.3f, ImVec2(hp.x + 70, hp.y + 4), cText, u.display.c_str());
            std::string meta = "@" + u.username + "  \xC2\xB7  joined " + RelTime(u.joined) + "  \xC2\xB7  " +
                               (u.online ? std::string("online now") : u.lastSeen > 0 ? "seen " + RelTime(u.lastSeen) : std::string("never online"));
            d->AddText(ImVec2(hp.x + 70, hp.y + 32), cDim, meta.c_str());
            ImGui::SetCursorScreenPos(ImVec2(hp.x, hp.y + 64));
            if (ImGui::Button("View profile", ImVec2(120, 28))) OpenPublicProfile(u.id, u.username, u.display, u.avatar);
            ImGui::Dummy(ImVec2(0, 8));

            // Roles.
            smallCaps("ROLES & BADGES");
            ImGui::Dummy(ImVec2(0, 2));
            static const char* kRoleBadges[] = {"admin", "staff", "moderator", "developer", "beta", "partner"};
            static const char* kRoleDesc[] = {"Everything, incl. giving staff / admin", "Roles, verification, bans, marketplace",
                                              "Moderates all forums, bans players, reviews listings", "Cosmetic badge", "Cosmetic badge", "Cosmetic badge"};
            for (int b = 0; b < 6; ++b) {
                const std::string bd = kRoleBadges[b];
                const bool has = std::find(u.badges.begin(), u.badges.end(), bd) != u.badges.end();
                const bool can = (myRank >= 3 || (myRank == 2 && b >= 2)) && !(self && bd == "admin");
                ImGui::PushID(b);
                ImGui::BeginDisabled(!can);
                bool v = has;
                if (ToggleSwitch("##role", &v)) {
                    AdminJob* j = job(3); j->s1 = u.id; j->s2 = bd; j->n1 = v ? 1 : 0; j->s3 = userQ; queue(j);
                    if (kDemo) { if (v) u.badges.push_back(bd); else u.badges.erase(std::remove(u.badges.begin(), u.badges.end(), bd), u.badges.end()); }
                }
                ImGui::EndDisabled();
                ImGui::SameLine(0, 10);
                const ImVec2 bp = ImGui::GetCursorScreenPos();
                const float pw = pill(d, ImVec2(bp.x, bp.y + 2), kRoleBadges[b], has ? badgeCol(bd) : IM_COL32(255, 255, 255, 20), IM_COL32_WHITE);
                ImGui::Dummy(ImVec2(pw + 4, 18));
                ImGui::SameLine(0, 6);
                ImGui::TextDisabled("%s", kRoleDesc[b]);
                ImGui::PopID();
            }
            if (myRank < 3) ImGui::TextDisabled("Only admins can give or take staff and admin.");
            ImGui::Dummy(ImVec2(0, 8));

            // Verification.
            smallCaps("VERIFICATION");
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::BeginDisabled(myRank < 2);
            bool vf = u.verified;
            if (ToggleSwitch("##verify", &vf)) {
                AdminJob* j = job(4); j->s1 = u.id; j->n1 = vf ? 1 : 0; j->s3 = userQ; queue(j);
                if (kDemo) u.verified = vf;
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 10);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(u.verified ? "Verified (blue check on their profile)" : "Not verified");
            ImGui::Dummy(ImVec2(0, 8));

            // Ban.
            smallCaps("SITE BAN");
            ImGui::Dummy(ImVec2(0, 2));
            const bool canBan = !self && myRank > theirRank;
            if (u.banned) {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1), "Banned %s", u.banUntil > 0 ? ("until " + FormatDate(u.banUntil)).c_str() : "permanently");
                if (!u.banReason.empty()) ImGui::TextWrapped("Reason: %s", u.banReason.c_str());
                if (ImGui::Button("Lift ban", ImVec2(120, 30))) {
                    AdminJob* j = job(6); j->s1 = u.id; j->s3 = userQ; queue(j);
                    if (kDemo) { u.banned = false; Toast("Ban lifted", 1); }
                }
            } else if (!canBan) {
                ImGui::TextDisabled(self ? "You can't ban yourself." : "You can only ban people ranked below you.");
            } else {
                ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, 420.0f));
                ImGui::InputTextWithHint("##banwhy", "Reason (they'll see this)", banReason, sizeof(banReason));
                ImGui::SetNextItemWidth(160);
                SlateCombo("##bandur", &banDur, "1 hour\0" "1 day\0" "7 days\0" "30 days\0" "Permanent\0");
                ImGui::SameLine(0, 8);
                static const long kHours[] = {1, 24, 24 * 7, 24 * 30, 0};
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.24f, 0.24f, 1));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.30f, 0.30f, 1));
                ImGui::BeginDisabled(!banReason[0]);
                if (ImGui::Button("Ban player", ImVec2(120, 0))) {
                    AdminJob* j = job(5); j->s1 = u.id; j->s2 = banReason; j->n1 = kHours[std::clamp(banDur, 0, 4)]; j->s3 = userQ; queue(j);
                    if (kDemo) { u.banned = true; u.banReason = banReason; Toast("Player banned", 1); }
                    banReason[0] = '\0';
                }
                ImGui::EndDisabled();
                ImGui::PopStyleColor(2);
                ImGui::TextDisabled("Banned players can't post, message, review, publish or send requests anywhere.");
            }
        }
        cardEnd();
    } else if (sub == 2) {   // ======================= Marketplace =======================
        static const char* kViews[] = {"Pending", "Approved", "Rejected"};
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine(0, 6);
            const bool on = mktView == i;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, okaytheme::WithAlpha(kAccent, 0.85f));
            char lbl[48];
            if (i == 0 && D.stats.pending > 0) std::snprintf(lbl, sizeof(lbl), "%s (%ld)", kViews[i], D.stats.pending);
            else std::snprintf(lbl, sizeof(lbl), "%s", kViews[i]);
            if (ImGui::Button(lbl, ImVec2(0, 30)) && !on) {
                mktView = i;
                AdminJob* j = job(7); j->s1 = i == 0 ? "pending" : i == 1 ? "approved" : "rejected"; queue(j);
                if (kDemo) for (auto& w : D.items) (void)w;
            }
            if (on) ImGui::PopStyleColor();
        }
        ImGui::SameLine(0, 14);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(mktView == 0 ? "New listings wait here; nobody else sees them until you approve." : mktView == 1 ? "Live on the marketplace." : "Only their author sees these, with your reason.");
        ImGui::Dummy(ImVec2(0, 6));
        const char* want = mktView == 0 ? "pending" : mktView == 1 ? "approved" : "rejected";
        ImGui::BeginChild("##ad_items", ImVec2(0, bodyH - 44), false);
        int shown = 0;
        long doId = 0; std::string doAct, doNote;
        for (auto& w : D.items) {
            if (w.status != want) continue;
            ++shown;
            ImGui::PushID((int)w.id);
            card("##item", 0, 0);
            ImDrawList* d = ImGui::GetWindowDrawList();
            const MktKind& k = kMktKinds[MktKindIndex(w.kind)];
            const ImVec2 p = ImGui::GetCursorScreenPos();
            d->AddCircleFilled(ImVec2(p.x + 5, p.y + 9), 5.0f, ImGui::GetColorU32(k.color), 12);
            d->AddText(ImVec2(p.x + 16, p.y), cDim, k.label);
            ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() + 2));
            ImGui::SetWindowFontScale(1.15f); ImGui::TextUnformatted(w.title.c_str()); ImGui::SetWindowFontScale(1.0f);
            ImGui::TextDisabled("by @%s  \xC2\xB7  submitted %s%s%s", w.author.c_str(), RelTime(w.created).c_str(),
                                w.featured ? "  \xC2\xB7  featured" : "", w.reviewsLocked ? "  \xC2\xB7  reviews off" : "");
            if (!w.description.empty()) { ImGui::PushTextWrapPos(0.0f); ImGui::TextUnformatted(w.description.c_str()); ImGui::PopTextWrapPos(); }
            if (!w.reviewNote.empty()) ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.40f, 1), "Note: %s", w.reviewNote.c_str());
            ImGui::Dummy(ImVec2(0, 4));
            const bool safe = ShellSafeUrl(w.url);
            const bool hosted = w.url.find("/storage/v1/object/public/workshop/") != std::string::npos;
            if (!hosted) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1), "Outside link \xE2\x80\x94 not hosted by OkaySpace, can't be approved.");
            ImGui::BeginDisabled(!safe);
            if (ImGui::Button("Download to check", ImVec2(0, 30))) OpenExternal(w.url);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", safe ? w.url.c_str() : "No valid link");
            ImGui::SameLine(0, 8);
            if (w.status != "approved") {
                ImGui::BeginDisabled(!hosted);
                if (PrimaryButton("Approve", ImVec2(110, 30))) { doId = w.id; doAct = "approve"; }
                ImGui::EndDisabled();
                ImGui::SameLine(0, 8);
            }
            if (w.status != "rejected") { if (ImGui::Button(w.status == "approved" ? "Unpublish\xE2\x80\xA6" : "Reject\xE2\x80\xA6", ImVec2(110, 30))) { rejectId = w.id; rejectNote[0] = '\0'; } ImGui::SameLine(0, 8); }
            if (w.status == "approved") {
                if (ImGui::Button(w.featured ? "Unfeature" : "Feature", ImVec2(100, 30))) { doId = w.id; doAct = w.featured ? "unfeature" : "feature"; }
                ImGui::SameLine(0, 8);
                if (ImGui::Button(w.reviewsLocked ? "Turn reviews on" : "Turn reviews off", ImVec2(140, 30))) { doId = w.id; doAct = w.reviewsLocked ? "unlock" : "lock"; }
                ImGui::SameLine(0, 8);
            }
            ImGui::BeginDisabled(myRank < 2);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.50f, 0.50f, 1));
            if (ImGui::Button("Delete", ImVec2(80, 30))) { doId = w.id; doAct = "delete"; }
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            if (rejectId == w.id) {
                ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - 220, 460.0f));
                ImGui::InputTextWithHint("##rnote", "Why? (the author sees this)", rejectNote, sizeof(rejectNote));
                ImGui::SameLine(0, 8);
                ImGui::BeginDisabled(!rejectNote[0]);
                if (ImGui::Button("Confirm", ImVec2(96, 0))) { doId = w.id; doAct = "reject"; doNote = rejectNote; rejectId = 0; }
                ImGui::EndDisabled();
                ImGui::SameLine(0, 6);
                if (ImGui::Button("Cancel", ImVec2(80, 0))) rejectId = 0;
            }
            cardEnd();
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::PopID();
        }
        if (!shown) ImGui::TextDisabled(g_adBusy ? "Loading\xE2\x80\xA6" : mktView == 0 ? "Nothing waiting for review. \xF0\x9F\x8E\x89" : "Nothing here.");
        ImGui::EndChild();
        if (doId) {
            AdminJob* j = job(8); j->n1 = doId; j->s1 = doAct; j->s2 = doNote; j->s3 = want; queue(j);
            if (kDemo) for (auto it = D.items.begin(); it != D.items.end(); ++it) if (it->id == doId) {
                if (doAct == "approve") { it->status = "approved"; Toast("Approved \xE2\x80\x94 it's live", 1); if (D.stats.pending) --D.stats.pending; }
                else if (doAct == "reject") { it->status = "rejected"; it->reviewNote = doNote; Toast("Rejected", 1); if (D.stats.pending) --D.stats.pending; }
                else if (doAct == "delete") { D.items.erase(it); Toast("Listing deleted", 1); }
                else if (doAct == "feature" || doAct == "unfeature") it->featured = doAct == "feature";
                else if (doAct == "lock" || doAct == "unlock") it->reviewsLocked = doAct == "lock";
                break;
            }
        }
    } else if (sub == 3) {   // ======================= Forums =======================
        const float aw = ImGui::GetContentRegionAvail().x;
        const float railW = std::min(260.0f, aw * 0.3f);
        card("##ad_secs", railW, bodyH);
        smallCaps("SECTIONS");
        ImGui::Dummy(ImVec2(0, 2));
        if (ImGui::Selectable("All posts", selSection == 0, 0, ImVec2(0, 26))) selSection = 0;
        for (const auto& s : D.sections) {
            ImGui::PushID((int)s.id);
            char lbl[96]; std::snprintf(lbl, sizeof(lbl), "o/%s", s.slug.c_str());
            if (ImGui::Selectable(lbl, selSection == s.id, 0, ImVec2(0, 26))) {
                selSection = s.id;
                AdminJob* j = job(10); j->n1 = s.id; queue(j);
            }
            ImGui::PopID();
        }
        if (!D.sectionsSup) ImGui::TextDisabled("Sections aren't set up (forum_sections.sql).");
        cardEnd();
        ImGui::SameLine(0, 14);
        ImGui::BeginChild("##ad_forum", ImVec2(0, bodyH), false);
        const okay::ForumSection* sec = nullptr;
        for (const auto& s : D.sections) if (s.id == selSection) sec = &s;
        if (sec) {
            card("##ad_sechead", 0, 0);
            ImGui::SetWindowFontScale(1.2f); ImGui::Text("o/%s", sec->slug.c_str()); ImGui::SetWindowFontScale(1.0f);
            ImGui::TextDisabled("%s  \xC2\xB7  %ld members  \xC2\xB7  %ld posts", sec->name.c_str(), sec->members, sec->threads);
            ImGui::Dummy(ImVec2(0, 6));
            smallCaps("SECTION ADMINS & MODERATORS");
            ImGui::Dummy(ImVec2(0, 2));
            int nStaff = 0;
            if (D.membersOf == sec->id) for (const auto& m : D.members) {
                if (m.role != "admin" && m.role != "moderator") continue;
                ++nStaff;
                ImGui::PushID(m.uid.c_str());
                ImGui::AlignTextToFramePadding();
                ImGui::Text("@%s", m.name.c_str());
                ImGui::SameLine(180);
                int r = m.role == "admin" ? 0 : 1;
                ImGui::SetNextItemWidth(130);
                ImGui::BeginDisabled(myRank < 2 && m.role == "admin");
                if (SlateCombo("##srole", &r, "Admin\0Moderator\0Member\0")) {
                    AdminJob* j = job(11); j->n1 = sec->id; j->s1 = m.uid; j->s2 = r == 0 ? "admin" : r == 1 ? "moderator" : "member"; queue(j);
                    if (kDemo) Toast("Role changed", 1);
                }
                ImGui::SameLine(0, 8);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.50f, 0.50f, 1));
                if (ImGui::Button("Remove from section", ImVec2(0, 0))) {
                    AdminJob* j = job(11); j->n1 = sec->id; j->s1 = m.uid; j->s2 = "remove"; queue(j);
                    if (kDemo) Toast("Removed from the section", 1);
                }
                ImGui::PopStyleColor();
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            if (!nStaff) ImGui::TextDisabled(g_adBusy ? "Loading\xE2\x80\xA6" : "No section admins or moderators.");
            if (myRank < 2) ImGui::TextDisabled("Staff and admins can change or remove section admins.");
            ImGui::Dummy(ImVec2(0, 6));
            static long confirmDel = 0;
            ImGui::BeginDisabled(myRank < 2);
            if (confirmDel != sec->id) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.50f, 0.50f, 1));
                if (ImGui::Button("Delete section\xE2\x80\xA6", ImVec2(140, 30))) confirmDel = sec->id;
                ImGui::PopStyleColor();
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.5f, 1), "Delete o/%s and all its posts?", sec->slug.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Yes, delete", ImVec2(110, 0))) { AdminJob* j = job(12); j->n1 = sec->id; queue(j); confirmDel = 0; selSection = 0; }
                ImGui::SameLine();
                if (ImGui::Button("Keep", ImVec2(70, 0))) confirmDel = 0;
            }
            ImGui::EndDisabled();
            cardEnd();
            ImGui::Dummy(ImVec2(0, 10));
        }
        card("##ad_posts", 0, 0);
        smallCaps(sec ? "POSTS IN THIS SECTION" : "RECENT POSTS");
        ImGui::Dummy(ImVec2(0, 2));
        int nPosts = 0;
        long tId = 0; std::string tAct;
        for (const auto& t : D.threads) {
            if (selSection != 0 && t.sectionId != selSection) continue;
            if (++nPosts > 60) break;
            ImGui::PushID((int)t.id);
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            ImDrawList* d = ImGui::GetWindowDrawList();
            float px = rp.x;
            if (t.pinned) px += pill(d, ImVec2(px, rp.y + 2), "PINNED", IM_COL32(67, 181, 129, 200), IM_COL32_WHITE) + 6;
            if (t.locked) px += pill(d, ImVec2(px, rp.y + 2), "COMMENTS OFF", IM_COL32(150, 156, 170, 200), IM_COL32_WHITE) + 6;
            const float bw = ImGui::GetContentRegionAvail().x;
            const ImVec4 clip(px, rp.y, rp.x + bw - 372, rp.y + 40);
            d->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(px, rp.y), cText, t.title.c_str(), nullptr, 0.0f, &clip);
            std::string meta = "@" + t.author + "  \xC2\xB7  " + RelTime(t.when);
            for (const auto& s : D.sections) if (s.id == t.sectionId) meta += "  \xC2\xB7  o/" + s.slug;
            const ImVec4 clip2(rp.x, rp.y, rp.x + bw - 372, rp.y + 46);
            d->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(rp.x, rp.y + 20), cDim, meta.c_str(), nullptr, 0.0f, &clip2);
            ImGui::SetCursorScreenPos(ImVec2(rp.x + bw - 360, rp.y + 4));
            if (ImGui::Button(t.pinned ? "Unpin" : "Pin", ImVec2(70, 28))) { tId = t.id; tAct = t.pinned ? "unpin" : "pin"; }
            ImGui::SameLine(0, 6);
            if (ImGui::Button(t.locked ? "Turn comments on" : "Turn comments off", ImVec2(160, 28))) { tId = t.id; tAct = t.locked ? "unlock" : "lock"; }
            ImGui::SameLine(0, 6);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.50f, 0.50f, 1));
            if (ImGui::Button("Remove", ImVec2(110, 28))) { tId = t.id; tAct = "delete"; }
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + 46));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
        if (!nPosts) ImGui::TextDisabled(g_adBusy ? "Loading\xE2\x80\xA6" : "No posts.");
        cardEnd();
        ImGui::EndChild();
        if (tId) {
            AdminJob* j = job(14); j->n1 = tId; j->s1 = tAct; queue(j);
            if (kDemo) for (auto it = D.threads.begin(); it != D.threads.end(); ++it) if (it->id == tId) {
                if (tAct == "delete") { D.threads.erase(it); Toast("Post removed", 1); }
                else if (tAct == "lock" || tAct == "unlock") { it->locked = tAct == "lock"; Toast(it->locked ? "Comments turned off" : "Comments turned on", 1); }
                else it->pinned = tAct == "pin";
                break;
            }
        }
    } else if (sub == 4) {   // ======================= Bans =======================
        card("##ad_bans", 0, 0);
        smallCaps("ACTIVE BANS");
        ImGui::Dummy(ImVec2(0, 4));
        if (D.bans.empty()) ImGui::TextDisabled(g_adBusy ? "Loading\xE2\x80\xA6" : "Nobody is banned.");
        std::string lift;
        for (const auto& b : D.bans) {
            ImGui::PushID(b.userId.c_str());
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddText(rp, cText, ("@" + (b.username.empty() ? b.userId.substr(0, 8) : b.username)).c_str());
            std::string meta = (b.until > 0 ? "until " + FormatDate(b.until) : std::string("permanent")) + "  \xC2\xB7  banned " + RelTime(b.when);
            if (!b.reason.empty()) meta += "  \xC2\xB7  " + b.reason;
            d->AddText(ImVec2(rp.x, rp.y + 20), cDim, meta.c_str());
            ImGui::SetCursorScreenPos(ImVec2(rp.x + ImGui::GetContentRegionAvail().x - 110, rp.y + 4));
            if (ImGui::Button("Lift ban", ImVec2(110, 28))) lift = b.userId;
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + 46));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
        if (!lift.empty()) {
            AdminJob* j = job(6); j->s1 = lift; queue(j);
            if (kDemo) D.bans.erase(std::remove_if(D.bans.begin(), D.bans.end(), [&](const okay::AdminBan& x) { return x.userId == lift; }), D.bans.end());
        }
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextDisabled("Ban someone from the Players tab.");
        cardEnd();
    } else {   // ======================= Audit log =======================
        card("##ad_log", 0, 0);
        smallCaps("AUDIT LOG");
        ImGui::Dummy(ImVec2(0, 4));
        logRows(D.log, 200);
        ImGui::Dummy(ImVec2(0, 4));
        if (ImGui::Button("Refresh", ImVec2(100, 30))) queue(job(16));
        cardEnd();
    }
}

// Banner for a banned player, at the top of every page.
void DrawBanBanner() {
    if (!g_myBan.banned) return;
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    std::string head = g_myBan.until > 0 ? "Your account is suspended until " + FormatDate(g_myBan.until) : std::string("Your account is suspended");
    std::string why = g_myBan.reason.empty() ? std::string("You can still play your games, but you can't post, message or publish.")
                                             : "Reason: " + g_myBan.reason + ". You can still play your games, but you can't post, message or publish.";
    const float h = 54.0f;
    d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(150, 40, 40, 230), 10.0f);
    d->AddText(ImVec2(p.x + 14, p.y + 8), IM_COL32_WHITE, head.c_str());
    d->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + 14, p.y + 28), IM_COL32(255, 220, 220, 255), why.c_str(), nullptr, w - 28);
    ImGui::Dummy(ImVec2(w, h + 8));
}

} // namespace okl
