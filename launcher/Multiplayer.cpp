// Multiplayer, Steam-style (Multiplayer.hpp): the game I'm in and how friends join it; joining a friend, an invite or a
// server; inviting; presence; the server browser's data; parties; the Multiplayer page with its dialogs, recent players
// and leaderboards. Moved out of launcher/main.cpp unchanged (docs/engine/modularization_plan.md, Part C section 16,
// batch L8).
#include "LauncherShared.hpp"
#include "Multiplayer.hpp"
#include "AntiCheatPages.hpp"     // TicketPreflightHolds
#include "CoverArtAvatars.hpp"    // DrawAvatar
#include "NavIcons.hpp"
#include "OkayTheme.hpp"
#include "DesktopNotify.hpp"        // InviteTabKey: one Notifications row per invite record
#include "JoinPlan.hpp"             // okay::joinplan::PlanJoin: which program a join starts
#include "okay/Core/BuildPipeline.hpp"   // ManifestVersionNear: an exported game's own version
#include "VersionsUI.hpp"           // RequestPage: "Get version X" opens the Versions page
#include "okay/Platform/Platform.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Friends.hpp"
#include "okay/Platform/GameIpc.hpp"
#include "okay/Platform/Parties.hpp"
#include "okay/Platform/Servers.hpp"
#include "okay/Net/JoinLink.hpp"
#include "okay/Net/Matchmaking.hpp"
#include "okay/Net/MatchResults.hpp"
#include "okay/Net/ServerQuery.hpp"

namespace okl {

// ============================================================================
// Multiplayer, Steam-style: the game I'm in and how friends join it, joining a
// friend / an invite / a server (the launcher starts the game with --join),
// inviting friends, and the server browser's data. The running game reports to
// us through okay/Platform/GameIpc.hpp files.
// ============================================================================
SDL_Window* g_launcherWindow = nullptr;   // brought forward when a game asks for the invite window

struct MyGame {
    bool running = false;
    long pid = 0;
    std::string title, status, join, invite;   // join/invite: links (invite carries the password)
    int players = 0, maxPlayers = 0;
    bool hosting = false;
};
static MyGame g_myGame;   // UI thread

static std::string MyUsername() {
    return g_presenceAccount && g_presenceAccount->IsLoggedIn() ? g_presenceAccount->CurrentSession().username : std::string();
}
static long PlayProcPid(const PlayProc& pp) {
#if defined(_WIN32)
    return pp.h ? (long)GetProcessId(pp.h) : 0;
#else
    return (long)pp.pid;
#endif
}
bool MpCanInvite() { return g_myGame.running && !g_myGame.invite.empty(); }
std::string MpGameTitle() { return g_myGame.running ? g_myGame.title : std::string(); }

// ---- Library lookup: which game a join link is for -------------------------------
// Only built (exported) games count in multiplayer — the same rule as the Play page.
struct MpLibGame { std::string title, scene; };
static std::vector<MpLibGame> MpLibrary(bool refresh = false) {
    static std::vector<MpLibGame> cache; static Uint32 at = 0;
    if (refresh || cache.empty() || okay::platform::TicksMs32() - at > 15000) {
        cache.clear(); at = okay::platform::TicksMs32();
        std::set<std::string> seen;
        for (const auto& sc : FindScenes()) {
            if (!IsBuiltGame(sc)) continue;   // exported games only, not editor projects / loose scenes
            const std::string t = GameTitleFromPath(sc.string());
            std::string low = t; for (auto& c : low) c = (char)std::tolower((unsigned char)c);
            if (seen.insert(low).second) cache.push_back({t, sc.string()});
        }
        std::sort(cache.begin(), cache.end(), [](const MpLibGame& a, const MpLibGame& b) { return a.title < b.title; });
    }
    return cache;
}
static bool SameTitle(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

// ---- Favorites + history (<config>/servers.cfg) ----------------------------------
struct MpSaved { std::string addr, game, name; long when = 0; };
static std::vector<MpSaved> g_mpFavs, g_mpHist;
static bool g_mpSavedLoaded = false;
static fs::path MpSavedFile() { return fs::path(g_pollCfgDir.empty() ? g_exeDir : g_pollCfgDir) / "servers.cfg"; }
static std::string MpClean(std::string v) { for (char& c : v) if (c == '\t' || c == '\n' || c == '\r') c = ' '; return v; }
static void MpLoadSaved() {
    if (g_mpSavedLoaded) return;
    g_mpSavedLoaded = true;
    std::ifstream f(MpSavedFile());
    std::string line;
    while (std::getline(f, line)) {
        std::vector<std::string> p; std::stringstream ss(line); std::string t;
        while (std::getline(ss, t, '\t')) p.push_back(t);
        if (p.size() < 2 || p[1].empty()) continue;
        MpSaved m; m.addr = p[1]; m.game = p.size() > 2 ? p[2] : ""; m.name = p.size() > 3 ? p[3] : "";
        m.when = p.size() > 4 ? std::atol(p[4].c_str()) : 0;
        (p[0] == "fav" ? g_mpFavs : g_mpHist).push_back(m);
    }
}
static void MpSaveSaved() {
    std::ofstream f(MpSavedFile(), std::ios::trunc);
    for (const auto& m : g_mpFavs) f << "fav\t" << MpClean(m.addr) << '\t' << MpClean(m.game) << '\t' << MpClean(m.name) << '\t' << m.when << '\n';
    for (const auto& m : g_mpHist) f << "hist\t" << MpClean(m.addr) << '\t' << MpClean(m.game) << '\t' << MpClean(m.name) << '\t' << m.when << '\n';
}
static bool MpIsFav(const std::string& addr) {
    MpLoadSaved();
    for (const auto& m : g_mpFavs) if (m.addr == addr) return true;
    return false;
}
static void MpToggleFav(const std::string& addr, const std::string& game, const std::string& name) {
    MpLoadSaved();
    auto it = std::find_if(g_mpFavs.begin(), g_mpFavs.end(), [&](const MpSaved& m) { return m.addr == addr; });
    if (it != g_mpFavs.end()) { g_mpFavs.erase(it); Toast("Removed from favorites", 1); }
    else { g_mpFavs.push_back({addr, game, name, (long)std::time(nullptr)}); Toast("Added to favorites", 1); }
    MpSaveSaved();
}
static void MpRemember(const okay::JoinLink& l) {   // history: direct servers only (relay codes don't last)
    if (l.addr.empty()) return;
    MpLoadSaved();
    g_mpHist.erase(std::remove_if(g_mpHist.begin(), g_mpHist.end(), [&](const MpSaved& m) { return m.addr == l.addr; }), g_mpHist.end());
    g_mpHist.insert(g_mpHist.begin(), {l.addr, l.game, l.name, (long)std::time(nullptr)});
    if (g_mpHist.size() > 50) g_mpHist.resize(50);
    MpSaveSaved();
}

// ---- Joining -------------------------------------------------------------------
static std::string g_mpPassLink;  static bool g_mpPassReq = false;   // ask for a password, then join
static std::string g_mpPickLink;  static bool g_mpPickReq = false;   // "which of your games is this?"
static okay::joinplan::Plan g_mpPlan; static bool g_mpPlanReq = false;   // "can't join yet": the versions differ / no editor
void MpLaunch(const okay::JoinLink& in) {
    okay::JoinLink l = in;
    const std::string link = l.Format();
    // Already playing it: the running game switches to this session.
    for (const auto& pp : g_playProcs)
        if (pp.key.rfind("game:", 0) == 0 && SameTitle(pp.title, l.game)) {
            okay::GameIpc::PostJoin(PlayProcPid(pp), link);
            Toast("Joining in " + pp.title + "\xE2\x80\xA6", 1);
            MpRemember(l);
            return;
        }
    // Which program (shared/JoinPlan.hpp): the player with an exported game (also for a dedicated server), the editor of
    // the right version with a project when the friend is playing their project in the editor, or a question.
    const auto lib = MpLibrary();
    std::vector<okay::joinplan::Candidate> cands;
    for (const auto& x : lib) {
        okay::joinplan::Candidate c; c.kind = okay::joinplan::Candidate::Kind::Exported; c.title = x.title; c.path = x.scene;
        c.gameVersion = okay::buildpipe::ManifestVersionNear("", x.scene);   // its build.json ("" = unknown: no version check)
        cands.push_back(c);
    }
    std::vector<std::string> editors;
    if (l.target == "project") {   // only then are the projects and editor versions read
        for (auto& c : MpJoinProjects()) cands.push_back(c);
        editors = MpInstalledEditors();
    }
    const okay::joinplan::Plan plan = okay::joinplan::PlanJoin(l, cands, editors);
    using JA = okay::joinplan::Action;
    if (plan.action == JA::AskWhichGame || plan.action == JA::Missing) { g_mpPickLink = link; g_mpPickReq = true; return; }   // ask
    if (plan.action == JA::Invalid) { Toast(plan.text, 2); return; }
    if (plan.action == JA::NeedEditor || plan.action == JA::VersionMismatch) { g_mpPlan = plan; g_mpPlanReq = true; return; }
    if (plan.action == JA::OpenEditor) {
        const okay::joinplan::Candidate& pc = cands[plan.index];
        if (!MpOpenProjectForJoin(plan.editorVersion, pc.path)) { Toast("Couldn't open " + pc.title + " in the editor v" + plan.editorVersion + ".", 2); return; }
        ImGui::SetClipboardText(link.c_str());   // the editor has no --join: Play, then join with the copied link
        MpRemember(l);
        Toast("Opening " + pc.title + " in the editor v" + plan.editorVersion + ". Press Play, then join " +
              (l.host.empty() ? std::string("the match") : l.host) + " (the join link is copied).", 1);
        return;
    }
    const MpLibGame* g = plan.index >= 0 && plan.index < (int)lib.size() ? &lib[plan.index] : nullptr;
    if (!g) { g_mpPickLink = link; g_mpPickReq = true; return; }
    const std::string player = FindExe({"OkaySpacePlayer.exe", "okay-player.exe", "okay-player"});
    if (player.empty()) { Toast("The OkaySpace player isn't installed next to the launcher.", 2); return; }
    if (!AntiCheatAllowsSceneLaunch(g->scene)) return;
    // A server that needs a session ticket: ask the ticket service first (off the UI thread). A banned answer refreshes the ban banner and
    // the game is not started; "required" without an answer doesn't start it either. When it passes, this runs again and goes through.
    if (TicketPreflightHolds(l, g->scene)) return;
    if (SpawnTracked(player, "\"" + g->scene + "\" --join \"" + link + "\"", "game:" + g->title, g->title)) {
        RecordPlayed(g->scene);
        SavePrefs();
        MpRemember(l);
        Toast("Starting " + g->title + " \xE2\x80\x94 joining " + (l.name.empty() ? l.Where() : l.name), 1);
    } else Toast("Couldn't start " + g->title + ".", 2);
}
// Matchmaking: the game (started now, or already running) joins the queue for a
// match of `players` in `mode` and connects when one is found (its own banner
// shows progress + Cancel). A party leader brings the party; members follow.
static void MpFindMatch(const std::string& game, const std::string& mode, int players, const std::string& region) {
    auto enc = [](const std::string& v) {
        std::string o;
        for (unsigned char c : v) { if (std::isalnum(c) || c == '-' || c == '_' || c == '.') o += (char)c; else { char b[4]; std::snprintf(b, sizeof(b), "%%%02X", c); o += b; } }
        return o;
    };
    const std::string link = "okay://match?mode=" + enc(mode) + "&players=" + std::to_string(players) +
                             "&min=" + std::to_string(players > 2 ? 2 : 0) + "&region=" + enc(region);
    for (const auto& pp : g_playProcs)
        if (pp.key.rfind("game:", 0) == 0 && SameTitle(pp.title, game)) {
            okay::GameIpc::PostJoin(PlayProcPid(pp), link);
            Toast("Finding a match in " + pp.title + "\xE2\x80\xA6", 1);
            return;
        }
    const MpLibGame* g = nullptr;
    const auto lib = MpLibrary();
    for (const auto& x : lib) if (SameTitle(x.title, game)) { g = &x; break; }
    if (!g) { Toast("Pick one of your games first.", 2); return; }
    const std::string player = FindExe({"OkaySpacePlayer.exe", "okay-player.exe", "okay-player"});
    if (player.empty()) { Toast("The OkaySpace player isn't installed next to the launcher.", 2); return; }
    std::string args = "\"" + g->scene + "\" --find-match";
    if (!mode.empty()) args += " \"" + mode + "\"";
    args += " --match-players " + std::to_string(players) + (players > 2 ? " --match-min 2" : "");
    if (!region.empty()) args += " --region \"" + region + "\"";
    if (!AntiCheatAllowsSceneLaunch(g->scene)) return;
    if (SpawnTracked(player, args, "game:" + g->title, g->title)) {
        RecordPlayed(g->scene); SavePrefs();
        Toast("Starting " + g->title + " \xE2\x80\x94 finding a match", 1);
    } else Toast("Couldn't start " + g->title + ".", 2);
}

// Join from a link (a friend's game, an invite card, the server browser, a paste).
void MpJoin(const std::string& text, const std::string& from) {
    (void)from;
    okay::JoinLink l;
    if (!okay::JoinLink::Parse(text, l)) { Toast("That isn't a join link.", 2); return; }
    if (l.needsPassword && l.password.empty()) { g_mpPassLink = l.Format(); g_mpPassReq = true; return; }
    MpLaunch(l);
}

// "Join game" on a friend: their presence carries a join link (older launchers
// put a bare relay code there, which can only be copied).
void MpJoinFriend(const std::string& sessionCode, const std::string& name) {
    okay::JoinLink l;
    if (okay::JoinLink::Parse(sessionCode, l)) { MpJoin(sessionCode, name); return; }
    ImGui::SetClipboardText(sessionCode.c_str());
    Toast("Session code " + sessionCode + " copied - open the game and Join with it", 2);
}

// ---- Inviting --------------------------------------------------------------------
static bool g_inviteOpenReq = false;            // open the Invite friends window
static int g_inviteMode = 0;                    // ...0 = to my game, 1 = to my party
static std::set<std::string> g_invitedUids;     // invited to the current session
static std::string g_invitedFor;                // ...which session that was
void MpInviteUser(const std::string& uid, const std::string& name) {
    if (!MpCanInvite()) { Toast("Host or join a match in your game first, then invite friends.", 2); return; }
    OutboxSendInvite(uid, InviteMessageBody(g_myGame.title, g_myGame.invite));   // card + game_invites record (expiry, answer)
    g_invitedUids.insert(uid);
    Toast("Invited " + name + " to " + g_myGame.title, 1);
}
void MpInviteGroup(long gid, const std::string& name) {
    if (!MpCanInvite()) { Toast("Host or join a match in your game first, then invite friends.", 2); return; }
    OutboxSend(std::string(), gid, InviteMessageBody(g_myGame.title, g_myGame.invite));
    Toast("Invited " + name + " to " + g_myGame.title, 1);
}
std::string MpMyJoin() { return g_myGame.running ? g_myGame.join : std::string(); }

// ---- Presence pump (once a second, UI thread) -----------------------------------
static int MpPublishWorker(void* p) {
    std::unique_ptr<std::pair<std::string, std::string>> j(static_cast<std::pair<std::string, std::string>*>(p));
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        if (a.IsLoggedIn()) okay::Friends(a).MarkPresence(j->first, j->second);
    } catch (...) {}
    return 0;
}
void PumpMultiplayer() {
    static Uint32 next = 0, lastPub = 0;
    static std::string pubText = "\x01", pubJoin;   // last published (impossible start value)
    const Uint32 now = okay::platform::TicksMs32();
    if (now < next) return;
    next = now + 1000;
    if (!g_presMutex) g_presMutex = okay::thr::CreateMutex();

    MyGame g;
    for (auto it = g_playProcs.rbegin(); it != g_playProcs.rend(); ++it) {   // newest game first
        if (it->key.rfind("game:", 0) != 0) continue;
        const long pid = PlayProcPid(*it);
        okay::GamePresence gp;
        if (!okay::GameIpc::ReadPresence(pid, gp)) {   // still loading (or an old player): just "Playing"
            if (!g.running) { g.running = true; g.pid = pid; g.title = it->title; }
            continue;
        }
        g = MyGame{};
        g.running = true; g.pid = pid; g.title = it->title; g.status = gp.status;
        g.players = gp.players; g.maxPlayers = gp.maxPlayers; g.hosting = gp.hosting;
        okay::JoinLink l;
        const std::string me = MyUsername();
        if (okay::JoinLink::Parse(gp.join, l))   { l.game = it->title; l.host = me; g.join = l.Format(); }
        if (okay::JoinLink::Parse(gp.invite, l)) { l.game = it->title; l.host = me; g.invite = l.Format(); }
        break;
    }
    if (!g.running && std::getenv("OKAY_MP_DEMO")) {   // dev screenshot hook: pretend a game is hosting
        g.running = true; g.title = "Star Arena"; g.status = "Hosting Maya's lobby"; g.players = 2; g.maxPlayers = 8; g.hosting = true;
        g.join = g.invite = "okay://join?game=Star%20Arena&addr=192.168.1.20:45000&name=Maya%27s%20lobby&host=kingimann";
    }
    if (g.join != g_myGame.join) { g_invitedUids.clear(); g_invitedFor = g.join; }   // a new session: invite again
    g_myGame = g;

    // What friends see (Invisible shares nothing; "share what I'm playing" off hides the game).
    std::string text, join;
    if (g.running && g_sharePresence) {
        text = "Playing " + g.title;
        if (!g.status.empty() && g.status != "In the menus") text += " \xC2\xB7 " + g.status;
        if (g.players > 0 && g.maxPlayers > 0) text += " (" + std::to_string(g.players) + "/" + std::to_string(g.maxPlayers) + ")";
        join = g.join;
    }
    okay::thr::LockMutex(g_presMutex); g_presText = text; g_presJoin = join; okay::thr::UnlockMutex(g_presMutex);
    const bool changed = text != pubText || join != pubJoin;
    const bool beat = g.running && now - lastPub > 60000;   // keep "online" fresh while playing
    if ((changed || beat) && now - lastPub > 3000 && g_onlineStatus != 2 && g_presenceAccount && g_presenceAccount->IsLoggedIn()) {
        pubText = text; pubJoin = join; lastPub = now;
        auto* job = new std::pair<std::string, std::string>(PresenceText(), join);
        if (okay::thr::Thread* t = okay::thr::CreateThread(MpPublishWorker, "okay-presence", job)) okay::thr::DetachThread(t);
        else delete job;
    }
    // Shift+Tab in a game (or net_invite_friends()): bring up Invite friends.
    if (!okay::GameIpc::TakeInviteRequests().empty()) {
        g_inviteOpenReq = true; g_inviteMode = 0;
        if (g_launcherWindow) { lw::Restore(g_launcherWindow); lw::Raise(g_launcherWindow); }
    }
}

// ---- Server browser data ---------------------------------------------------------
struct MpRow {
    okay::ServerInfo info;          // the live answer (responded=false: what the listing / link said)
    std::string game, region;       // listing / link details
    std::string friendName, link;   // Friends tab: who, and their join link
    bool relayed = false;
};
static okay::thr::Mutex* g_mpMutex = nullptr;
static std::vector<MpRow> g_mpRows;     // (guarded) last results
static int g_mpRowsTab = -1;            // (guarded) which tab they're for
static volatile bool g_mpBusy = false;
struct MpQueryJob { int tab = 0; std::vector<MpRow> seed; };
static int MpQueryWorker(void* p) {
    std::unique_ptr<MpQueryJob> job(static_cast<MpQueryJob*>(p));
    std::vector<MpRow> rows = job->seed;
    try {
        if (job->tab == 0) {   // Internet: the public listing
            okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
            if (a.IsLoggedIn())
                for (const auto& gs : okay::Matchmaking::BrowseWith(a)) {
                    MpRow r;
                    r.info.host = gs.hostAddr; r.info.port = (std::uint16_t)gs.port; r.info.name = gs.name;
                    r.info.players = gs.players; r.info.maxPlayers = gs.maxPlayers; r.info.map = gs.map;
                    r.info.password = gs.hasPassword; r.info.dedicated = gs.dedicated;
                    r.game = gs.game; r.region = gs.region;
                    rows.push_back(r);
                }
        }
        std::vector<okay::QueryTarget> targets; std::vector<std::size_t> idx;
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (!rows[i].relayed && !rows[i].info.host.empty() && rows[i].info.port) {
                targets.push_back({rows[i].info.host, rows[i].info.port}); idx.push_back(i);
            }
        const auto res = okay::QueryServers(targets, 1500, /*lan=*/job->tab == 2);
        for (std::size_t k = 0; k < idx.size() && k < res.size(); ++k) {
            MpRow& r = rows[idx[k]];
            if (!res[k].responded) continue;
            const std::string keepGame = r.game;
            r.info = res[k];
            if (r.info.name.empty() && !keepGame.empty()) r.info.name = keepGame;
            r.game = !res[k].game.empty() ? res[k].game : keepGame;
        }
        for (std::size_t k = idx.size(); k < res.size(); ++k) { MpRow r; r.info = res[k]; r.game = res[k].game; rows.push_back(r); }
    } catch (...) {}
    if (!g_mpMutex) g_mpMutex = okay::thr::CreateMutex();
    okay::thr::LockMutex(g_mpMutex); g_mpRows = std::move(rows); g_mpRowsTab = job->tab; okay::thr::UnlockMutex(g_mpMutex);
    g_mpBusy = false;
    return 0;
}
static void MpRefresh(int tab) {
    if (g_mpBusy) return;
    if (!g_mpMutex) g_mpMutex = okay::thr::CreateMutex();
    auto* job = new MpQueryJob; job->tab = tab;
    MpLoadSaved();
    auto seedSaved = [&](const std::vector<MpSaved>& v) {
        for (const auto& m : v) {
            MpRow r; std::string h; std::uint16_t port = 0;
            if (!okay::SplitHostPort(m.addr, h, port)) continue;
            r.info.host = h; r.info.port = port; r.info.name = m.name; r.game = m.game;
            job->seed.push_back(r);
        }
    };
    if (tab == 1) {   // Friends: the joinable games in their presence
        std::vector<okay::Friend> fl;
        if (g_frMutex) { okay::thr::LockMutex(g_frMutex); fl = g_frList; okay::thr::UnlockMutex(g_frMutex); }
        for (const auto& f : fl) {
            okay::JoinLink l;
            if (f.status != "accepted" || !f.online || !okay::JoinLink::Parse(f.sessionCode, l)) continue;
            MpRow r;
            r.friendName = f.display.empty() ? f.username : f.display;
            r.link = l.Format(); r.relayed = l.Relayed(); r.game = l.game;
            r.info.name = !l.name.empty() ? l.name : r.friendName + "'s game";
            r.info.password = l.needsPassword;
            std::string h; std::uint16_t port = 0;
            if (!l.addr.empty() && okay::SplitHostPort(l.addr, h, port)) { r.info.host = h; r.info.port = port; }
            job->seed.push_back(r);
        }
    } else if (tab == 3) seedSaved(g_mpFavs);
    else if (tab == 4) seedSaved(g_mpHist);
    g_mpBusy = true;
    if (okay::thr::Thread* t = okay::thr::CreateThread(MpQueryWorker, "okay-servers", job)) okay::thr::DetachThread(t);
    else { delete job; g_mpBusy = false; }
}
// Kick a background Friends refresh (the invite window / Friends tab need the list).
static void MpFetchFriends() {
    static Uint32 last = 0;   // an empty list stays empty: don't refetch every frame
    if (g_frFetching || !g_presenceAccount || !g_presenceAccount->IsLoggedIn()) return;
    if (last && okay::platform::TicksMs32() - last < 30000) return;
    last = okay::platform::TicksMs32();
    if (!g_frMutex) g_frMutex = okay::thr::CreateMutex();
    g_frFetching = true;
    if (okay::thr::Thread* t = okay::thr::CreateThread(FriendsListWorker, "okay-frlist", nullptr)) okay::thr::DetachThread(t);
    else g_frFetching = false;
}

// ---- Parties: friends who play together -------------------------------------------
// The leader's launcher stores its game session on the party; members' launchers
// follow it into the same match. A background thread keeps the party fresh
// (every 3s while in one, 15s otherwise); actions run on short-lived threads.
static okay::thr::Mutex* g_partyMutex = nullptr;
static okay::Party g_partyNew;                 // (guarded) latest fetch
static bool g_partyNewReady = false;           // (guarded)
static std::string g_partyMe;                  // (guarded) my uid
static std::string g_partyToast;               // (guarded) a result to show
static int g_partyToastKind = 1;               // (guarded)
static volatile bool g_partyForce = false;     // refetch now
static volatile bool g_partyPollRun = false;
static okay::Party g_party;                    // UI thread copy
static std::string g_partyMyUid;               // UI thread copy
static std::vector<std::pair<std::string, std::string>> g_partyPendingInvites;   // (uid, name) waiting for a new party
static bool PartyDemo() { return std::getenv("OKAY_MP_DEMO") != nullptr; }

static int PartyPollWorker(void*) {
    Uint32 next = 0;
    while (g_partyPollRun) {
        const Uint32 now = okay::platform::TicksMs32();
        if (now >= next || g_partyForce) {
            g_partyForce = false;
            bool inParty = false;
            try {
                okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
                if (a.IsLoggedIn()) {
                    okay::Parties ps(a);
                    okay::Party p;
                    if (ps.Mine(p)) {
                        inParty = p.Valid();
                        const std::string me = ps.MyId();
                        okay::thr::LockMutex(g_partyMutex);
                        g_partyNew = std::move(p); g_partyMe = me; g_partyNewReady = true;
                        okay::thr::UnlockMutex(g_partyMutex);
                    }
                }
            } catch (...) {}
            next = okay::platform::TicksMs32() + (inParty ? 3000 : 15000);
        }
        okay::platform::DelayMs(100);
    }
    return 0;
}

struct PartyJob { int op = 0; std::string a, b; bool flag = false; std::string partyId; };
enum { kPartyCreate = 1, kPartyJoin, kPartyLeave, kPartyKick, kPartyPromote, kPartyReady, kPartySession };
static int PartyJobWorker(void* p) {
    std::unique_ptr<PartyJob> j(static_cast<PartyJob*>(p));
    std::string err, ok;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Parties ps(a);
        bool done = false;
        switch (j->op) {
        case kPartyCreate:  done = ps.Create(&err);  ok = "Party created \xE2\x80\x94 invite your friends"; break;
        case kPartyJoin:    done = ps.Join(j->a, &err); ok = "You joined the party"; break;
        case kPartyLeave:   done = ps.Leave(&err);   ok = "You left the party"; break;
        case kPartyKick:    done = ps.Kick(j->a, &err); ok = "Removed " + j->b + " from the party"; break;
        case kPartyPromote: done = ps.Promote(j->a, &err); ok = j->b + " is the party leader now"; break;
        case kPartyReady:   done = ps.SetReady(j->partyId, j->flag); break;
        case kPartySession: done = ps.SetSession(j->partyId, j->a, j->b); break;
        }
        if (!done && err.empty() && j->op < kPartyReady) err = "Couldn't update the party.";
    } catch (...) { err = "Couldn't reach the server."; }
    if (!err.empty() || !ok.empty()) {
        okay::thr::LockMutex(g_partyMutex);
        g_partyToast = err.empty() ? ok : err; g_partyToastKind = err.empty() ? 1 : 2;
        okay::thr::UnlockMutex(g_partyMutex);
    }
    g_partyForce = true;
    return 0;
}
static void PartyRun(int op, const std::string& a = {}, const std::string& b = {}, bool flag = false) {
    if (PartyDemo()) {   // dev screenshot hook: act locally
        if (op == kPartyLeave) g_party = okay::Party{};
        if (op == kPartyReady) for (auto& m : g_party.members) if (m.uid == g_partyMyUid) m.ready = flag;
        return;
    }
    if (!g_partyMutex) g_partyMutex = okay::thr::CreateMutex();
    auto* j = new PartyJob; j->op = op; j->a = a; j->b = b; j->flag = flag; j->partyId = g_party.id;
    if (okay::thr::Thread* t = okay::thr::CreateThread(PartyJobWorker, "okay-party", j)) okay::thr::DetachThread(t);
    else delete j;
}
static bool PartyIsLeader() { return g_party.Valid() && g_party.leader == g_partyMyUid; }
std::string MpPartyId() { return g_party.id; }

// Invite someone to my party (making one first if I'm not in a party).
void MpPartyInvite(const std::string& uid, const std::string& name) {
    if (!g_party.Valid()) {
        g_partyPendingInvites.push_back({uid, name});
        PartyRun(kPartyCreate);
        return;
    }
    if (g_party.Find(uid)) { Toast(name + " is already in your party", 1); return; }
    if ((int)g_party.members.size() >= g_party.maxSize) { Toast("Your party is full", 2); return; }
    OutboxSend(uid, 0, okay::Parties::InviteBody(g_party.id, MyUsername()));
    Toast("Invited " + name + " to your party", 1);
}
void MpPartyJoinFromInvite(const std::string& body) {
    std::string pid, from;
    if (!okay::Parties::ParseInvite(body, pid, from)) { Toast("That party invite isn't valid.", 2); return; }
    if (g_party.id == pid) { Toast("You're already in that party", 1); return; }
    PartyRun(kPartyJoin, pid);
}

// Once a frame: take fresh party state, keep the leader's session on the party,
// and follow the leader into games.
void PumpParty() {
    if (PartyDemo()) {
        static bool seeded = false;
        if (!seeded) {
            seeded = true;
            g_partyMyUid = "me";
            g_party.id = "demo-party"; g_party.leader = "me"; g_party.game = "Star Arena"; g_party.maxSize = 8;
            auto mk = [](const char* uid, const char* user, const char* disp, bool ready, bool online, const char* playing) {
                okay::PartyMember m; m.uid = uid; m.username = user; m.display = disp; m.ready = ready; m.online = online; m.playing = playing; return m; };
            g_party.members = {mk("me", "kingimann", "", true, true, "Playing Star Arena \xC2\xB7 Hosting Maya's lobby (2/8)"),
                               mk("demo-maya", "maya", "Maya Chen", true, true, "Playing Star Arena \xC2\xB7 Playing on Maya's lobby"),
                               mk("demo-theo", "theo", "Theo", false, true, "In OkaySpace")};
        }
        return;
    }
    if (!g_presenceAccount || !g_presenceAccount->IsLoggedIn()) {
        if (g_party.Valid()) g_party = okay::Party{};
        return;
    }
    if (!g_partyMutex) g_partyMutex = okay::thr::CreateMutex();
    if (!g_partyPollRun) {
        g_partyPollRun = true;
        if (okay::thr::Thread* t = okay::thr::CreateThread(PartyPollWorker, "okay-partypoll", nullptr)) okay::thr::DetachThread(t);
        else g_partyPollRun = false;
    }
    std::string toast; int toastKind = 1;
    bool fresh = false; okay::Party np;
    okay::thr::LockMutex(g_partyMutex);
    if (g_partyNewReady) { np = g_partyNew; g_partyMyUid = g_partyMe; g_partyNewReady = false; fresh = true; }
    toast.swap(g_partyToast); toastKind = g_partyToastKind;
    okay::thr::UnlockMutex(g_partyMutex);
    if (!toast.empty()) Toast(toast, toastKind);
    if (fresh) {
        // Who came and went (not on the first load / when switching parties).
        if (g_party.Valid() && np.id == g_party.id) {
            for (const auto& m : np.members) if (!g_party.Find(m.uid) && m.uid != g_partyMyUid) Toast(m.Name() + " joined the party", 1);
            for (const auto& m : g_party.members) if (!np.Find(m.uid) && m.uid != g_partyMyUid) Toast(m.Name() + " left the party", 1);
            if (np.leader != g_party.leader && np.leader == g_partyMyUid) Toast("You're the party leader now", 1);
        } else if (g_party.Valid() && !np.Valid()) Toast("You're no longer in the party", 1);
        g_party = np;
        if (g_party.Valid() && !g_partyPendingInvites.empty()) {   // invites waiting for a new party
            auto pend = std::move(g_partyPendingInvites); g_partyPendingInvites.clear();
            for (const auto& [uid, name] : pend) MpPartyInvite(uid, name);
        }
    }
    if (!g_party.Valid()) return;

    static Uint32 lastSync = 0;
    if (PartyIsLeader()) {   // members follow whatever session I'm in
        const std::string game = MpCanInvite() ? g_myGame.title : std::string();
        const std::string link = MpCanInvite() ? g_myGame.invite : std::string();
        if ((game != g_party.game || link != g_party.joinLink) && okay::platform::TicksMs32() - lastSync > 3000) {
            lastSync = okay::platform::TicksMs32();
            g_party.game = game; g_party.joinLink = link;   // optimistic; the next poll confirms
            PartyRun(kPartySession, game, link);
        }
        return;
    }
    // A member: follow the leader into their match (once per session).
    static std::string followed;
    if (!g_partyFollow || g_party.joinLink.empty() || g_party.joinLink == followed) return;
    followed = g_party.joinLink;
    okay::JoinLink theirs, mine;
    if (okay::JoinLink::Parse(g_party.joinLink, theirs) && okay::JoinLink::Parse(MpMyJoin(), mine) && mine.SameSession(theirs)) return;
    const okay::PartyMember* lead = g_party.Find(g_party.leader);
    Toast("Following " + (lead ? lead->Name() : std::string("the leader")) + " into " + (g_party.game.empty() ? std::string("their game") : g_party.game), 1);
    MpJoin(g_party.joinLink, lead ? lead->Name() : std::string());
}

// ---- Multiplayer UI: dialogs (drawn every frame) --------------------------------
static bool g_mpConnectReq = false;    // open "Connect to server"
static bool MpDemo() { static const bool on = std::getenv("OKAY_MP_DEMO") != nullptr; return on; }

// Friends for the invite window: the Friends list (fetched in the background).
static std::vector<okay::Friend> MpFriends() {
    std::vector<okay::Friend> fl;
    if (MpDemo()) {
        const long t0 = (long)std::time(nullptr);
        const char* names[] = {"maya", "theo", "rin", "jules", "sam"};
        const char* disp[]  = {"Maya Chen", "Theo", "Rin", "Jules Park", "Sam"};
        for (int i = 0; i < 5; ++i) {
            okay::Friend f; f.id = std::string("demo-") + names[i]; f.username = names[i]; f.display = disp[i];
            f.status = "accepted"; f.online = i < 3; f.lastSeen = t0 - (i < 3 ? 30 : 7200 * i);
            f.playing = i == 0 ? "Playing Star Arena \xC2\xB7 Hosting Maya's lobby (2/8)" : i == 1 ? "In OkaySpace" : i == 2 ? "Playing Kart Rush" : "";
            if (i == 0) { okay::JoinLink l; l.game = "Star Arena"; l.addr = "192.168.1.20:45000"; l.name = "Maya's lobby"; l.host = "maya"; f.sessionCode = l.Format(); }
            fl.push_back(f);
        }
        return fl;
    }
    if (g_frMutex) { okay::thr::LockMutex(g_frMutex); fl = g_frList; okay::thr::UnlockMutex(g_frMutex); }
    if (fl.empty()) MpFetchFriends();
    return fl;
}

// "Maya invited you to play Star Arena  [Join] [Later]" — bottom-right pop-ups for
// invites that arrive while the chat is closed (Steam's invite toast).
static void DrawInvitePopups() {
    struct Pop { PendingInvite inv; Uint32 shownAt = 0; };
    static std::vector<Pop> pops;
    std::vector<PendingInvite> fresh;
    if (g_pollMutex) { okay::thr::LockMutex(g_pollMutex); fresh.swap(g_pollInvites); okay::thr::UnlockMutex(g_pollMutex); }
    static bool demoDone = false;
    if (MpDemo() && !demoDone) {
        demoDone = true;
        PendingInvite d; d.id = 1; d.fromName = "Maya Chen"; d.when = (long)std::time(nullptr);
        d.body = InviteMessageBody("Star Arena", "okay://join?game=Star%20Arena&addr=192.168.1.20:45000&name=Maya%27s%20lobby&host=maya");
        fresh.push_back(d);
    }
    std::sort(fresh.begin(), fresh.end(), [](const PendingInvite& a, const PendingInvite& b) { return a.id < b.id; });
    for (const auto& f : fresh) {
        if (f.id <= g_inviteSeenId && !MpDemo()) continue;
        if (f.id > g_inviteSeenId) { g_inviteSeenId = f.id; SavePrefs(); }
        ChatInviteInfo ci;
        const bool party = f.body.rfind("[Party] ", 0) == 0;
        if (!party && !ParseChatInvite(f.body, ci)) continue;
        pops.push_back({f, okay::platform::TicksMs32()});
        if (pops.size() > 3) pops.erase(pops.begin());
        UpsertNotif(okl::notify::InviteTabKey(f.id, party ? 0 : ci.link.inviteId), party ? f.fromName + " invited you to their party"
                                                            : f.fromName + " invited you to play " + ci.title,
                    party ? "Open the Multiplayer page to join." : "Open the chat with " + f.fromName + " to join.", 2, 14);
        PlayChatChime();
    }
    if (pops.empty()) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = 340.0f, h = 104.0f;
    float y = vp->WorkPos.y + vp->WorkSize.y - 70.0f;
    for (int i = (int)pops.size() - 1; i >= 0; --i) {
        Pop& pp = pops[i];
        if (okay::platform::TicksMs32() - pp.shownAt > 60000) { pops.erase(pops.begin() + i); continue; }
        y -= h + 10;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - w - 18, y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, okaytheme::WithAlpha(okaytheme::kRaised, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, okaytheme::WithAlpha(okaytheme::SuccessFill(), 0.9f));
        char wid[32]; std::snprintf(wid, sizeof(wid), "##invpop%ld", pp.inv.id);
        bool close = false;
        if (ImGui::Begin(wid, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
            const bool party = pp.inv.body.rfind("[Party] ", 0) == 0;
            ChatInviteInfo ci; if (!party) ParseChatInvite(pp.inv.body, ci);
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            d->AddRectFilled(p, ImVec2(p.x + 44, p.y + 44), ImGui::GetColorU32(kAccent), 10.0f);
            DrawGamepadIcon(d, ImVec2(p.x + 22, p.y + 22), 27.0f, okaytheme::OnColorU32(0.941f));
            ImGui::SetCursorScreenPos(ImVec2(p.x + 54, p.y));
            ImGui::BeginGroup();
            ImGui::TextUnformatted(FitText(pp.inv.fromName + (party ? " invited you to a party" : " invited you to play"), w - 80).c_str());
            ImGui::SetWindowFontScale(1.1f);
            ImGui::TextUnformatted(FitText(party ? std::string("Join their party") : ci.title, w - 80).c_str());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::EndGroup();
            ImGui::SetCursorScreenPos(ImVec2(p.x + 54, p.y + 54));
            ImGui::PushStyleColor(ImGuiCol_Button, okaytheme::SuccessFill());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, okaytheme::SuccessFill());
            if (ImGui::Button(party ? "Join party" : "Join game", ImVec2(130, 28))) {
                if (party) MpPartyJoinFromInvite(pp.inv.body); else MpJoin(ci.link.Format(), pp.inv.fromName);
                close = true;
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            if (ImGui::Button("Later", ImVec2(okl::FitButtonW("Later", okl::UiPx(90.0f)), okl::UiPx(28.0f)))) close = true;
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        if (close) pops.erase(pops.begin() + i);
    }
}

void DrawMpDialogs() {
    DrawInvitePopups();
    const ImU32 cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    // ---- Invite friends (Steam overlay's "Invite to game") ----
    if (g_inviteOpenReq) { g_inviteOpenReq = false; ImGui::OpenPopup("Invite friends##mpinv"); }
    ImGui::SetNextWindowSize(ImVec2(440, 520), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Invite friends##mpinv", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + 46, p.y + 46), ImGui::GetColorU32(kAccent), 10.0f);
        DrawGamepadIcon(dl, ImVec2(p.x + 23, p.y + 23), 28.0f, okaytheme::OnColorU32(0.941f));
        ImGui::SetCursorScreenPos(ImVec2(p.x + 58, p.y + 2));
        ImGui::BeginGroup();
        const bool toParty = g_inviteMode == 1;
        ImGui::SetWindowFontScale(1.12f);
        ImGui::TextUnformatted(toParty ? "Invite to your party" : g_myGame.running ? g_myGame.title.c_str() : "No game running");
        ImGui::SetWindowFontScale(1.0f);
        if (toParty) {
            if (g_party.Valid()) ImGui::TextDisabled("%d/%d in the party Â· they'll join your games with you", (int)g_party.members.size(), g_party.maxSize);
            else ImGui::TextDisabled("Inviting someone starts a party with you as the leader.");
        } else if (g_myGame.running) {
            std::string st = g_myGame.status.empty() ? std::string("Starting\xE2\x80\xA6") : g_myGame.status;
            if (g_myGame.maxPlayers > 0) st += "  \xC2\xB7  " + std::to_string(g_myGame.players) + "/" + std::to_string(g_myGame.maxPlayers) + " players";
            ImGui::TextDisabled("%s", st.c_str());
        } else ImGui::TextDisabled("Start a game from your library first.");
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 56));
        if (!toParty && !MpCanInvite()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("Friends can join once you host or join a match in the game. Invites send them a link that opens the game and drops them into your session.");
            ImGui::PopTextWrapPos();
        }
        static char filter[64] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##invfilter", "Search friends", filter, sizeof(filter));
        std::string fl = filter; for (auto& c : fl) c = (char)std::tolower((unsigned char)c);
        auto friends = MpFriends();
        std::stable_sort(friends.begin(), friends.end(), [](const okay::Friend& a, const okay::Friend& b) { return a.online > b.online; });
        ImGui::BeginChild("##invlist", ImVec2(0, -44), ImGuiChildFlags_None);
        int shown = 0;
        for (const auto& f : friends) {
            if (f.status != "accepted") continue;
            const std::string name = f.display.empty() ? f.username : f.display;
            if (!fl.empty()) {
                std::string a = name + " " + f.username; for (auto& c : a) c = (char)std::tolower((unsigned char)c);
                if (a.find(fl) == std::string::npos) continue;
            }
            ++shown;
            ImGui::PushID(f.id.c_str());
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, rowH = 50.0f;
            ImDrawList* ld = ImGui::GetWindowDrawList();
            if (ImGui::IsMouseHoveringRect(rp, ImVec2(rp.x + w, rp.y + rowH)) && ImGui::IsWindowHovered())
                ld->AddRectFilled(rp, ImVec2(rp.x + w, rp.y + rowH), okaytheme::Ink(10), 8.0f);
            ImGui::SetCursorScreenPos(ImVec2(rp.x + 6, rp.y + 7));
            DrawAvatar(f.avatar, name, 36.0f);
            ld->AddCircleFilled(ImVec2(rp.x + 6 + 31, rp.y + 7 + 31), 5.5f, f.online ? okaytheme::SuccessFillU32() : okaytheme::TextDimU32(), 12);
            const float bw = 92.0f;
            ld->AddText(ImVec2(rp.x + 52, rp.y + 8), ImGui::GetColorU32(ImGuiCol_Text), FitText(name, w - 52 - bw - 12).c_str());
            const std::string sub = !f.online ? (f.lastSeen > 0 ? LastOnline(f.lastSeen) : std::string("Offline"))
                                  : !f.playing.empty() ? f.playing : std::string("Online");
            ld->AddText(ImVec2(rp.x + 52, rp.y + 27), cDim, FitText(sub, w - 52 - bw - 12).c_str());
            ImGui::SetCursorScreenPos(ImVec2(rp.x + w - bw - 4, rp.y + 10));
            static std::set<std::string> partyInvited;
            const bool inParty = toParty && g_party.Find(f.id);
            const bool sent = toParty ? partyInvited.count(f.id) != 0 : g_invitedUids.count(f.id) != 0;
            ImGui::BeginDisabled(sent || inParty || (!toParty && !MpCanInvite()));
            if (inParty) ImGui::Button("In party", ImVec2(bw, 30));
            else if (sent) ImGui::Button("Invited", ImVec2(bw, 30));
            else if (PrimaryButton("Invite", ImVec2(bw, 30))) {
                if (toParty) { MpPartyInvite(f.id, name); partyInvited.insert(f.id); } else MpInviteUser(f.id, name);
            }
            ImGui::EndDisabled();
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rowH + 2));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
        if (shown == 0) {
            ImGui::Dummy(ImVec2(0, 20));
            ImGui::TextDisabled(friends.empty() ? (g_frFetching ? "Loading friends\xE2\x80\xA6" : "Add friends on the Friends page to invite them.")
                                                : "No friends match.");
        }
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::BeginDisabled(toParty || !MpCanInvite());
        if (ImGui::Button("Copy invite link", ImVec2(okl::FitButtonW("Copy invite link", okl::UiPx(150.0f)), okl::UiPx(30.0f)))) { ImGui::SetClipboardText(g_myGame.invite.c_str()); Toast("Invite link copied \xE2\x80\x94 anyone with it can join", 1); }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Paste it anywhere; in OkaySpace it opens as a Join card.");
        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
        if (ImGui::Button("Done", ImVec2(okl::FitButtonW("Done", okl::UiPx(84.0f)), okl::UiPx(30.0f))) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Password for a locked server ----
    if (g_mpPassReq) { g_mpPassReq = false; ImGui::OpenPopup("Password required##mppass"); }
    if (ImGui::BeginPopupModal("Password required##mppass", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char pw[96] = "";
        okay::JoinLink l; okay::JoinLink::Parse(g_mpPassLink, l);
        ImGui::Text("%s is password protected.", l.name.empty() ? "This server" : l.name.c_str());
        if (ImGui::IsWindowAppearing()) { pw[0] = '\0'; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(300);
        const bool enter = ImGui::InputTextWithHint("##mppw", "Password", pw, sizeof(pw), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Dummy(ImVec2(0, 4));
        if ((PrimaryButton("Join", ImVec2(okl::FitButtonW("Join", okl::UiPx(120.0f)), okl::UiPx(30.0f))) || enter) && pw[0]) { l.password = pw; ImGui::CloseCurrentPopup(); MpLaunch(l); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(okl::FitButtonW("Cancel", okl::UiPx(100.0f)), okl::UiPx(30.0f)))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Can't join yet: the game's version differs from the host's, or the editor version is not installed ----
    if (g_mpPlanReq) { g_mpPlanReq = false; ImGui::OpenPopup("Can't join yet##mpplan"); }
    ImGui::SetNextWindowSize(ImVec2(okl::UiPx(440.0f), 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Can't join yet##mpplan", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + okl::UiPx(420.0f));
        ImGui::TextUnformatted(g_mpPlan.text.c_str());
        ImGui::PopTextWrapPos();
        if (!g_mpPlan.theirVersion.empty()) {
            ImGui::Dummy(ImVec2(0, 4));
            const bool mismatch = g_mpPlan.action == okay::joinplan::Action::VersionMismatch;
            ImGui::TextDisabled(mismatch ? "Their version" : "Editor needed"); ImGui::SameLine(okl::UiPx(130.0f));
            ImGui::TextUnformatted(g_mpPlan.theirVersion.c_str());
            if (mismatch) { ImGui::TextDisabled("Your version"); ImGui::SameLine(okl::UiPx(130.0f)); ImGui::TextUnformatted(g_mpPlan.yourVersion.c_str()); }
        }
        ImGui::Dummy(ImVec2(0, 6));
        const std::string get = okay::joinplan::GetVersionLabel(g_mpPlan);
        if (!get.empty() && PrimaryButton(get.c_str(), ImVec2(okl::FitButtonW(get.c_str(), okl::UiPx(160.0f)), okl::UiPx(30.0f)))) {
            okayversionsui::RequestPage(g_mpPlan.theirVersion);   // (the main loop switches to the Versions page)
            ImGui::CloseCurrentPopup();
        }
        if (!get.empty()) ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(okl::FitButtonW("Close", okl::UiPx(100.0f)), okl::UiPx(30.0f))) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Which game? (the link's game isn't in the library by that name) ----
    if (g_mpPickReq) { g_mpPickReq = false; ImGui::OpenPopup("Choose the game##mppick"); }
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Choose the game##mppick", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        okay::JoinLink l; okay::JoinLink::Parse(g_mpPickLink, l);
        ImGui::PushTextWrapPos(400);
        if (!l.game.empty() && l.game != "game")
            ImGui::Text("\"%s\" isn't in your library. Get it from the Market, or pick the game to start if you have it under another name:", l.game.c_str());
        else ImGui::TextUnformatted("Which game is this server for?");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 4));
        const auto lib = MpLibrary();
        ImGui::BeginChild("##mppicklist", ImVec2(400, std::min(240.0f, 34.0f * (float)std::max<std::size_t>(1, lib.size()) + 8)), ImGuiChildFlags_Borders);
        for (const auto& g : lib)
            if (ImGui::Selectable(g.title.c_str(), false, 0, ImVec2(0, 26))) {
                l.game = g.title; ImGui::CloseCurrentPopup(); MpLaunch(l);
            }
        if (lib.empty()) ImGui::TextDisabled("Your library is empty.");
        ImGui::EndChild();
        if (ImGui::Button("Cancel", ImVec2(okl::FitButtonW("Cancel", okl::UiPx(100.0f)), okl::UiPx(30.0f)))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Connect to server (by address) ----
    if (g_mpConnectReq) { g_mpConnectReq = false; ImGui::OpenPopup("Connect to server##mpconn"); }
    if (ImGui::BeginPopupModal("Connect to server##mpconn", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char addr[128] = "", pw[96] = "";
        static int gameIdx = 0;
        const auto lib = MpLibrary();
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::TextDisabled("Address");
        ImGui::SetNextItemWidth(340);
        ImGui::InputTextWithHint("##mpaddr", "203.0.113.7:45000 or play.example.com", addr, sizeof(addr));
        ImGui::TextDisabled("Game");
        ImGui::SetNextItemWidth(340);
        if (gameIdx >= (int)lib.size()) gameIdx = 0;
        if (BeginSlateCombo("##mpgame", lib.empty() ? "(no games in your library)" : lib[gameIdx].title.c_str())) {
            for (int i = 0; i < (int)lib.size(); ++i) if (ImGui::Selectable(lib[i].title.c_str(), i == gameIdx)) gameIdx = i;
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Password (if it has one)");
        ImGui::SetNextItemWidth(340);
        ImGui::InputText("##mpcpw", pw, sizeof(pw), ImGuiInputTextFlags_Password);
        ImGui::TextDisabled("You can also paste an okay://join link as the address.");
        ImGui::Dummy(ImVec2(0, 4));
        std::string h; std::uint16_t port = 0;
        okay::JoinLink pasted;
        const bool isLink = okay::JoinLink::Parse(addr, pasted);
        const bool ok = isLink || (okay::SplitHostPort(addr, h, port) && !lib.empty());
        ImGui::BeginDisabled(!ok);
        if (PrimaryButton("Connect", ImVec2(okl::FitButtonW("Connect", okl::UiPx(120.0f)), okl::UiPx(30.0f)))) {
            okay::JoinLink l = pasted;
            if (!isLink) { l = okay::JoinLink{}; l.game = lib[gameIdx].title; l.addr = h + ":" + std::to_string(port); }
            if (pw[0]) l.password = pw;
            ImGui::CloseCurrentPopup();
            MpLaunch(l);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(okl::FitButtonW("Cancel", okl::UiPx(100.0f)), okl::UiPx(30.0f)))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---- Party card (Multiplayer page) ----------------------------------------------------
static void DrawCrown(ImDrawList* d, ImVec2 c, float s, ImU32 col) {
    const ImVec2 pts[] = {ImVec2(c.x - s, c.y + s * 0.55f), ImVec2(c.x - s, c.y - s * 0.35f), ImVec2(c.x - s * 0.5f, c.y + s * 0.05f),
                          ImVec2(c.x, c.y - s * 0.6f), ImVec2(c.x + s * 0.5f, c.y + s * 0.05f), ImVec2(c.x + s, c.y - s * 0.35f),
                          ImVec2(c.x + s, c.y + s * 0.55f)};
    d->AddConvexPolyFilled(pts, 7, col);
}
static void DrawPartyCard() {
    const ImVec4 cardBg = okaytheme::kRaised;
    const ImU32 cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::BeginChild("##party", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
    const float right = ImGui::GetContentRegionMax().x;
    if (!g_party.Valid()) {
        ImGui::SetWindowFontScale(0.82f);
        ImGui::TextColored(okaytheme::Current().info, "PARTY");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextUnformatted("Play together with friends");
        ImGui::TextDisabled("The party leader picks the game and server \xE2\x80\x94 everyone in the party joins with them.");
        ImGui::SetCursorPos(ImVec2(right - 290, 18));
        if (PrimaryButton("Create party", ImVec2(okl::FitButtonW("Create party", okl::UiPx(140.0f)), okl::UiPx(32.0f)))) PartyRun(kPartyCreate);
        ImGui::SameLine();
        if (ImGui::Button("Invite to party", ImVec2(okl::FitButtonW("Invite to party", okl::UiPx(140.0f)), okl::UiPx(32.0f)))) { g_inviteMode = 1; g_inviteOpenReq = true; }
        ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor();
        return;
    }
    const bool leader = PartyIsLeader();
    const okay::PartyMember* me = g_party.Find(g_partyMyUid);
    // Header row: PARTY · 3/8, what we're playing, actions.
    ImGui::SetWindowFontScale(0.82f);
    ImGui::TextColored(okaytheme::Current().info, "PARTY  \xC2\xB7  %d/%d", (int)g_party.members.size(), g_party.maxSize);
    ImGui::SetWindowFontScale(1.0f);
    const okay::PartyMember* lead = g_party.Find(g_party.leader);
    std::string what = !g_party.game.empty() ? (leader ? "You're playing " : (lead ? lead->Name() : std::string("The leader")) + " is playing ") + g_party.game
                     : leader ? std::string("Start a match \xE2\x80\x94 your party joins you") : (lead ? lead->Name() : std::string("The leader")) + " hasn't started a game yet";
    ImGui::TextUnformatted(what.c_str());
    ImGui::SetCursorPos(ImVec2(right - 360, 16));
    if (ImGui::Button("Invite", ImVec2(okl::FitButtonW("Invite", okl::UiPx(100.0f)), okl::UiPx(30.0f)))) { g_inviteMode = 1; g_inviteOpenReq = true; }
    ImGui::SameLine();
    const bool ready = me && me->ready;
    if (ImGui::Button(ready ? "Ready" : "Not ready", ImVec2(110, 30))) PartyRun(kPartyReady, {}, {}, !ready);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(ready ? "You're ready. Click to un-ready." : "Let the party know you're ready.");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, okaytheme::Current().danger);
    if (ImGui::Button("Leave", ImVec2(okl::FitButtonW("Leave", okl::UiPx(110.0f)), okl::UiPx(30.0f)))) PartyRun(kPartyLeave);
    ImGui::PopStyleColor();
    if (!leader && !g_party.joinLink.empty()) {   // jump in now (e.g. after turning Follow off)
        ImGui::SetCursorPos(ImVec2(right - 360, 52));
        ImGui::PushStyleColor(ImGuiCol_Button, okaytheme::SuccessFill());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, okaytheme::SuccessFill());
        if (ImGui::Button("Join the leader", ImVec2(okl::FitButtonW("Join the leader", okl::UiPx(342.0f)), okl::UiPx(26.0f)))) MpJoin(g_party.joinLink, lead ? lead->Name() : std::string());
        ImGui::PopStyleColor(2);
    }
    ImGui::Dummy(ImVec2(0, 6));
    // Members: avatar chips with crown / ready tick / status.
    const float chipW = 170.0f, chipH = 54.0f;
    const int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 8) / (chipW + 8)));
    const ImVec2 g0 = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    int i = 0;
    for (const auto& m : g_party.members) {
        const ImVec2 cp(g0.x + (i % cols) * (chipW + 8), g0.y + (i / cols) * (chipH + 8));
        ++i;
        ImGui::PushID(m.uid.c_str());
        ImGui::SetCursorScreenPos(cp);
        ImGui::InvisibleButton("##chip", ImVec2(chipW, chipH));
        const bool hov = ImGui::IsItemHovered();
        d->AddRectFilled(cp, ImVec2(cp.x + chipW, cp.y + chipH), hov ? okaytheme::Ink(16) : okaytheme::Ink(8), 10.0f);
        if (ImGui::BeginPopupContextItem("##chipctx")) {
            if (m.uid != g_partyMyUid) {
                if (ImGui::MenuItem("Message")) OpenChatWith(m.uid, m.Name(), m.avatar);
                if (ImGui::MenuItem("View profile")) OpenPublicProfile(m.uid, m.username, m.Name(), m.avatar, 0);
                if (leader) {
                    ImGui::Separator();
                    if (ImGui::MenuItem("Make party leader")) PartyRun(kPartyPromote, m.uid, m.Name());
                    ImGui::PushStyleColor(ImGuiCol_Text, okaytheme::Current().danger);
                    if (ImGui::MenuItem("Remove from party")) PartyRun(kPartyKick, m.uid, m.Name());
                    ImGui::PopStyleColor();
                }
            } else ImGui::TextDisabled("That's you");
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(ImVec2(cp.x + 8, cp.y + 9));
        DrawAvatar(m.avatar, m.Name(), 36.0f);
        d->AddCircleFilled(ImVec2(cp.x + 8 + 31, cp.y + 9 + 31), 5.5f, m.online ? okaytheme::SuccessFillU32() : okaytheme::TextDimU32(), 12);
        if (m.uid == g_party.leader) DrawCrown(d, ImVec2(cp.x + 8 + 18, cp.y + 6), 6.0f, okaytheme::WarningFillU32());
        const std::string nm = m.uid == g_partyMyUid ? m.Name() + " (you)" : m.Name();
        d->AddText(ImVec2(cp.x + 52, cp.y + 9), ImGui::GetColorU32(ImGuiCol_Text), FitText(nm, chipW - 60).c_str());
        const std::string st = m.ready ? std::string("Ready") : !m.playing.empty() ? m.playing : m.online ? std::string("Online") : std::string("Offline");
        d->AddText(ImVec2(cp.x + 52, cp.y + 28), m.ready ? okaytheme::SuccessU32() : cDim, FitText(st, chipW - 60).c_str());
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(g0.x, g0.y + ((i + cols - 1) / cols) * (chipH + 8)));
    if (leader) ImGui::TextDisabled("You lead: when you host or join a match, your party joins you. Right-click a member to make them leader.");
    else if (ImGui::Checkbox("Follow the leader into games", &g_partyFollow)) SavePrefs();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---- The Multiplayer page: now playing + server browser ------------------------------
// ---- Server info (who's playing) + Quick Play ----------------------------------------
struct MpInfoJob { std::string host; std::uint16_t port = 0; };
static okay::thr::Mutex* g_mpInfoMutex = nullptr;
static std::vector<okay::ServerPlayer> g_mpInfoPlayers;   // (guarded)
static int g_mpInfoState = 0;                            // (guarded) 0 idle, 1 asking, 2 answered, 3 no answer
static int MpInfoWorker(void* p) {
    std::unique_ptr<MpInfoJob> j(static_cast<MpInfoJob*>(p));
    std::vector<okay::ServerPlayer> pl;
    const bool ok = okay::QueryPlayers(j->host, j->port, pl, 2000);
    okay::thr::LockMutex(g_mpInfoMutex); g_mpInfoPlayers = std::move(pl); g_mpInfoState = ok ? 2 : 3; okay::thr::UnlockMutex(g_mpInfoMutex);
    return 0;
}
static MpRow g_mpInfoRow;
static bool g_mpInfoReq = false;
static void MpOpenInfo(const MpRow& r) {
    g_mpInfoRow = r; g_mpInfoReq = true;
    if (!g_mpInfoMutex) g_mpInfoMutex = okay::thr::CreateMutex();
    okay::thr::LockMutex(g_mpInfoMutex); g_mpInfoPlayers.clear(); g_mpInfoState = 1; okay::thr::UnlockMutex(g_mpInfoMutex);
    if (MpDemo()) {
        okay::thr::LockMutex(g_mpInfoMutex);
        g_mpInfoPlayers = {{"Maya Chen", "", 24, 1520}, {"theo", "", 17, 610}, {"rin", "", 9, 95}, {"kartking", "", 3, 2410}};
        g_mpInfoState = 2;
        okay::thr::UnlockMutex(g_mpInfoMutex);
        return;
    }
    if (r.relayed || r.info.host.empty()) { okay::thr::LockMutex(g_mpInfoMutex); g_mpInfoState = 3; okay::thr::UnlockMutex(g_mpInfoMutex); return; }
    auto* j = new MpInfoJob{r.info.host, r.info.port};
    if (okay::thr::Thread* t = okay::thr::CreateThread(MpInfoWorker, "okay-srvinfo", j)) okay::thr::DetachThread(t);
    else delete j;
}
static void DrawMpInfoPopup(const std::function<void(const MpRow&)>& join) {
    if (g_mpInfoReq) { g_mpInfoReq = false; ImGui::OpenPopup("Server info##mpinfo"); }
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Server info##mpinfo", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const MpRow& r = g_mpInfoRow;
    ImGui::SetWindowFontScale(1.12f);
    ImGui::TextUnformatted(r.info.name.empty() ? r.info.Address().c_str() : r.info.name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    std::string det = (r.game.empty() ? std::string("Unknown game") : r.game);
    if (!r.info.map.empty()) det += "  \xC2\xB7  " + r.info.map;
    if (r.info.maxPlayers > 0) det += "  \xC2\xB7  " + std::to_string(r.info.players) + "/" + std::to_string(r.info.maxPlayers) + " players";
    if (r.info.pingMs >= 0) det += "  \xC2\xB7  " + std::to_string(r.info.pingMs) + " ms";
    ImGui::TextDisabled("%s", det.c_str());
    std::string flags = r.relayed ? std::string("Through a relay") : r.info.Address();
    if (r.info.dedicated) flags += "  \xC2\xB7  Dedicated";
    if (r.info.auth) flags += "  \xC2\xB7  Sign-in required";
    if (r.info.encrypted) flags += "  \xC2\xB7  Encrypted";
    if (r.info.password) flags += "  \xC2\xB7  Password";
    ImGui::TextDisabled("%s", flags.c_str());
    ImGui::Separator();
    std::vector<okay::ServerPlayer> pl; int st = 0;
    okay::thr::LockMutex(g_mpInfoMutex); pl = g_mpInfoPlayers; st = g_mpInfoState; okay::thr::UnlockMutex(g_mpInfoMutex);
    if (st == 1) ImGui::TextDisabled("Asking the server who's playing\xE2\x80\xA6");
    else if (st == 3) ImGui::TextDisabled(r.relayed ? "Player lists aren't available through a relay." : "The server didn't answer.");
    else if (pl.empty()) ImGui::TextDisabled("Nobody's playing right now \xE2\x80\x94 be the first.");
    else if (ImGui::BeginTable("##mpplayers", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(440, std::min(260.0f, 30.0f + 26.0f * pl.size())))) {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        std::sort(pl.begin(), pl.end(), [](const okay::ServerPlayer& a, const okay::ServerPlayer& b) { return a.score > b.score; });
        for (const auto& p : pl) {
            ImGui::TableNextRow(0, 24.0f);
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(p.name.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d", p.score);
            ImGui::TableSetColumnIndex(2);
            const int secs = (int)p.seconds;
            if (secs >= 3600) ImGui::TextDisabled("%dh %02dm", secs / 3600, secs / 60 % 60); else ImGui::TextDisabled("%dm %02ds", secs / 60, secs % 60);
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 4));
    if (PrimaryButton("Join", ImVec2(okl::FitButtonW("Join", okl::UiPx(130.0f)), okl::UiPx(30.0f)))) { ImGui::CloseCurrentPopup(); join(r); }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(okl::FitButtonW("Close", okl::UiPx(100.0f)), okl::UiPx(30.0f))) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Quick Play: the best open server for a game — not full, no password, low ping,
// preferring ones with people in them — across the public list and the LAN.
struct MpQuickJob { std::string game; std::vector<std::string> library; };
static okay::thr::Mutex* g_mpQuickMutex = nullptr;
static volatile bool g_mpQuickBusy = false;
static std::string g_mpQuickLink, g_mpQuickMsg;   // (guarded) result
static int MpQuickWorker(void* p) {
    std::unique_ptr<MpQuickJob> j(static_cast<MpQuickJob*>(p));
    std::vector<MpRow> rows;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        if (a.IsLoggedIn())
            for (const auto& gs : okay::Matchmaking::BrowseWith(a, j->game)) {
                MpRow r; r.info.host = gs.hostAddr; r.info.port = (std::uint16_t)gs.port; r.game = gs.game;
                rows.push_back(r);
            }
    } catch (...) {}
    std::vector<okay::QueryTarget> targets;
    for (const auto& r : rows) targets.push_back({r.info.host, r.info.port});
    const auto res = okay::QueryServers(targets, 1500, /*lan=*/true);
    const okay::ServerInfo* best = nullptr; std::string bestGame; int bestScore = 1 << 30;
    for (std::size_t i = 0; i < res.size(); ++i) {
        const okay::ServerInfo& s = res[i];
        if (!s.responded || s.password || (s.maxPlayers > 0 && s.players >= s.maxPlayers)) continue;
        const std::string game = !s.game.empty() ? s.game : (i < rows.size() ? rows[i].game : std::string());
        if (!j->game.empty() ? !SameTitle(game, j->game)
                             : std::none_of(j->library.begin(), j->library.end(), [&](const std::string& t) { return SameTitle(t, game); })) continue;
        const int score = s.pingMs + (s.players == 0 ? 150 : 0) - std::min(s.players, 10) * 5;
        if (score < bestScore) { bestScore = score; best = &s; bestGame = game; }
    }
    std::string link, msg;
    if (best) {
        okay::JoinLink l; l.game = bestGame; l.addr = best->Address(); l.name = best->name;
        link = l.Format();
    } else msg = "No open servers for " + (j->game.empty() ? std::string("your games") : j->game) + " right now \xE2\x80\x94 host a match and invite friends.";
    okay::thr::LockMutex(g_mpQuickMutex); g_mpQuickLink = link; g_mpQuickMsg = msg; okay::thr::UnlockMutex(g_mpQuickMutex);
    g_mpQuickBusy = false;
    return 0;
}
static void MpQuickPlay(const std::string& game) {
    if (g_mpQuickBusy) return;
    if (!g_mpQuickMutex) g_mpQuickMutex = okay::thr::CreateMutex();
    auto* j = new MpQuickJob; j->game = game;
    for (const auto& g : MpLibrary()) j->library.push_back(g.title);
    g_mpQuickBusy = true;
    if (okay::thr::Thread* t = okay::thr::CreateThread(MpQuickWorker, "okay-quickplay", j)) okay::thr::DetachThread(t);
    else { delete j; g_mpQuickBusy = false; }
}
static void PumpQuickPlay() {
    if (!g_mpQuickMutex) return;
    std::string link, msg;
    okay::thr::LockMutex(g_mpQuickMutex); link.swap(g_mpQuickLink); msg.swap(g_mpQuickMsg); okay::thr::UnlockMutex(g_mpQuickMutex);
    if (!msg.empty()) Toast(msg, 2);
    if (!link.empty()) MpJoin(link, std::string());
}

// ---- Multiplayer: Recent players + Leaderboards (match results, docs/sql/matches.sql) ----
// Fetched on short-lived worker threads; the UI copies results under g_mrMutex.
static okay::thr::Mutex* g_mrMutex = nullptr;
static volatile bool g_mrBusy = false;
static std::vector<okay::RecentPlayer> g_mrRecent;          // (guarded) from match reports
static bool g_mrRecentLoaded = false, g_mrRecentSupported = true;   // (guarded)
static std::vector<okay::PlayerStats> g_mrBoard;            // (guarded)
static okay::PlayerStats g_mrMine; static bool g_mrHaveMine = false;  // (guarded)
static std::string g_mrBoardKey;                            // (guarded) "game|stat" the board is for
static bool g_mrBoardSupported = true;                      // (guarded)
struct MrJob { int kind = 0; std::string game, stat; };     // 0 recent players, 1 leaderboard
static int MatchResultsWorker(void* p) {
    std::unique_ptr<MrJob> j(static_cast<MrJob*>(p));
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::MatchResults mr(a);
        if (j->kind == 0) {
            bool sup = true;
            auto rows = mr.RecentPlayers(60, &sup);
            okay::thr::LockMutex(g_mrMutex);
            g_mrRecent = std::move(rows); g_mrRecentSupported = sup; g_mrRecentLoaded = true;
            okay::thr::UnlockMutex(g_mrMutex);
        } else {
            bool sup = true;
            auto rows = mr.Leaderboard(j->game, j->stat, 100, &sup);
            okay::PlayerStats mine; const bool have = sup && mr.MyStats(j->game, mine);
            okay::thr::LockMutex(g_mrMutex);
            g_mrBoard = std::move(rows); g_mrMine = mine; g_mrHaveMine = have;
            g_mrBoardSupported = sup; g_mrBoardKey = j->game + "|" + j->stat;
            okay::thr::UnlockMutex(g_mrMutex);
        }
    } catch (...) {}
    g_mrBusy = false;
    return 0;
}
static void MrQueue(int kind, const std::string& game = std::string(), const std::string& stat = std::string()) {
    if (!g_mrMutex) g_mrMutex = okay::thr::CreateMutex();
    if (g_mrBusy || g_pollCfgDir.empty()) return;
    g_mrBusy = true;
    MrJob* j = new MrJob{kind, game, stat};   // (built outside: okay::thr::CreateThread is a macro on Windows)
    if (okay::thr::Thread* t = okay::thr::CreateThread(MatchResultsWorker, "okay-matches", j)) okay::thr::DetachThread(t);
    else { g_mrBusy = false; delete j; }
}
static std::string MrDuration(long secs) {
    char b[32];
    if (secs < 60) std::snprintf(b, sizeof(b), "%lds", secs);
    else if (secs < 3600) std::snprintf(b, sizeof(b), "%ldm", secs / 60);
    else std::snprintf(b, sizeof(b), "%ld.%ldh", secs / 3600, (secs % 3600) / 360);
    return b;
}

// Everyone I've been in a game with: match reports (signed-in servers) merged
// with what my games logged locally (any session, even ones nobody reported).
static void DrawRecentPlayers(bool signedIn, bool reload) {
    struct Row { std::string uid, username, name, avatar, game; long when = 0; int matches = 0; bool fromServer = false; };
    static std::vector<okay::PlayedWith> local;
    static Uint32 localAt = 0;
    static bool asked = false;
    if (reload || !localAt || okay::platform::TicksMs32() - localAt > 5000) { local = okay::GameIpc::ReadPlayedWith(); localAt = okay::platform::TicksMs32(); }
    if (signedIn && (reload || !asked) && !MpDemo()) { asked = true; MrQueue(0); }

    std::vector<Row> rows;
    bool supported = true;
    if (MpDemo()) {
        const long t0 = (long)std::time(nullptr);
        const char* u[] = {"maya", "theo", "kai_88", "rin", "nova"};
        const char* d[] = {"Maya Chen", "Theo", "Kai", "Rin", "Nova"};
        const char* g[] = {"Star Arena", "Star Arena", "Kart Rush", "Star Arena", "Block Party"};
        const long ago[] = {600, 3600 * 3, 86400, 86400 * 2, 86400 * 6};
        const int m[] = {7, 2, 1, 4, 1};
        for (int i = 0; i < 5; ++i) { Row r; r.uid = std::string("demo-") + u[i]; r.username = u[i]; r.name = d[i]; r.game = g[i]; r.when = t0 - ago[i]; r.matches = m[i]; r.fromServer = true; rows.push_back(r); }
        Row loc; loc.name = "guest_4417"; loc.game = "Kart Rush"; loc.when = t0 - 86400 * 3; rows.push_back(loc);
    } else {
        if (g_mrMutex) {
            okay::thr::LockMutex(g_mrMutex);
            supported = g_mrRecentSupported;
            for (const auto& p : g_mrRecent) { Row r; r.uid = p.userId; r.username = p.username; r.name = p.Name(); r.avatar = p.avatar; r.game = p.game; r.when = p.when; r.matches = p.matches; r.fromServer = true; rows.push_back(r); }
            okay::thr::UnlockMutex(g_mrMutex);
        }
        const std::string me = MyUsername();
        for (const auto& p : local) {
            bool dup = false;
            for (auto& r : rows)
                if ((!p.userId.empty() && r.uid == p.userId) || (p.userId.empty() && !r.username.empty() && SameTitle(r.username, p.name))) {
                    if (p.when > r.when) { r.when = p.when; r.game = p.game; }
                    dup = true; break;
                }
            if (dup || (!me.empty() && SameTitle(p.name, me))) continue;
            Row r; r.uid = p.userId; r.name = p.name; r.game = p.game; r.when = p.when; rows.push_back(r);
        }
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.when > b.when; });
    }

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextDisabled("People you've been in a game with. Open a profile to add them as a friend, or invite them to what you're playing.");
    ImGui::Dummy(ImVec2(0, 4));
    const bool canInvite = !g_myGame.invite.empty();
    ImGui::BeginChild("##mprecent", ImVec2(0, 0), ImGuiChildFlags_None);
    if (rows.empty()) {
        const char* msg = g_mrBusy ? "Loading\xE2\x80\xA6"
                        : !signedIn ? "Sign in to see who you've played with."
                        : "Nobody yet. Play a multiplayer game and the people you meet show up here.";
        const ImVec2 ts = ImGui::CalcTextSize(msg), av = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2((av.x - ts.x) * 0.5f, std::max(20.0f, av.y * 0.4f)));
        ImGui::TextDisabled("%s", msg);
    }
    const ImVec4 cardBg = okaytheme::kRaised;
    for (int i = 0; i < (int)rows.size(); ++i) {
        const Row& r = rows[i];
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
        ImGui::BeginChild("##rp", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
        DrawAvatar(r.avatar, r.name, 40.0f);
        ImGui::SameLine(0, 12);
        ImGui::BeginGroup();
        ImGui::TextUnformatted(r.name.empty() ? r.username.c_str() : r.name.c_str());
        if (!r.username.empty() && r.username != r.name) { ImGui::SameLine(); ImGui::TextDisabled("@%s", r.username.c_str()); }
        std::string det = r.game.empty() ? std::string() : r.game;
        const std::string ago = AgoText(r.when);
        if (!ago.empty()) det += (det.empty() ? "" : "  \xC2\xB7  ") + ago;
        if (r.matches > 1) det += "  \xC2\xB7  " + std::to_string(r.matches) + " matches together";
        if (r.uid.empty()) det += "  \xC2\xB7  not signed in";
        ImGui::TextDisabled("%s", det.c_str());
        ImGui::EndGroup();
        if (!r.uid.empty()) {
            const float right = ImGui::GetContentRegionMax().x;
            ImGui::SameLine(right - 236);
            const float by = ImGui::GetCursorPosY() + 4;
            ImGui::SetCursorPosY(by);
            ImGui::BeginDisabled(!canInvite);
            if (ImGui::Button("Invite", ImVec2(okl::FitButtonW("Invite", okl::UiPx(110.0f)), okl::UiPx(32.0f)))) MpInviteUser(r.uid, r.name);
            ImGui::EndDisabled();
            if (!canInvite && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Host or join a match in a game first.");
            ImGui::SameLine();
            ImGui::SetCursorPosY(by);
            if (ImGui::Button("Profile", ImVec2(okl::FitButtonW("Profile", okl::UiPx(110.0f)), okl::UiPx(32.0f)))) OpenPublicProfile(r.uid, r.username, r.name, r.avatar, 0);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 2));
    }
    if (signedIn && !supported && !MpDemo())
        ImGui::TextDisabled("Match history isn't set up on the server yet (docs/sql/matches.sql), so only games on this PC are listed.");
    ImGui::EndChild();
}
// The same list on the Friends page's "Add friend" view (people you met in a game are the likeliest new friends).
void DrawRecentPlayersPanel(bool signedIn) { DrawRecentPlayers(signedIn, false); }

// Per-game leaderboards from match results, plus my own line.
static void DrawGameLeaderboards(bool signedIn, bool reload) {
    static int gameSel = 0, statSel = 0;
    const auto lib = MpLibrary();
    int nStats = 0; const char* const* statKeys = okay::MatchResults::Stats(&nStats);
    const char* statNames[] = {"Wins", "Total score", "Best score", "Matches", "Time played"};
    if (gameSel >= (int)lib.size()) gameSel = 0;
    std::string game = MpDemo() ? std::string("Star Arena") : (lib.empty() ? std::string() : lib[gameSel].title);
    if (!g_myGame.title.empty() && gameSel == 0 && !MpDemo()) {   // default to what I'm playing
        for (int i = 0; i < (int)lib.size(); ++i) if (SameTitle(lib[i].title, g_myGame.title)) { gameSel = i; game = lib[i].title; break; }
    }

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetNextItemWidth(220);
    if (BeginSlateCombo("##lbgame", game.empty() ? "No games in your library" : game.c_str())) {
        for (int i = 0; i < (int)lib.size(); ++i) if (ImGui::Selectable(lib[i].title.c_str(), gameSel == i)) { gameSel = i; reload = true; }
        ImGui::EndCombo();
    }
    for (int i = 0; i < nStats; ++i) {
        ImGui::SameLine(0, i ? 6 : 14);
        const bool on = statSel == i;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? kAccent : okaytheme::InkV(0.05f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? kAccent : okaytheme::InkV(0.1f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
        if (ImGui::Button(statNames[i], ImVec2(0, 26)) && !on) { statSel = i; reload = true; }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }
    const std::string stat = statKeys[statSel];
    const std::string key = game + "|" + stat;

    std::vector<okay::PlayerStats> board;
    okay::PlayerStats mine; bool haveMine = false, supported = true, current = false;
    if (MpDemo()) {
        const char* u[] = {"maya", "nova", "theo", "kai_88", "rin", "jules"};
        const char* d[] = {"Maya Chen", "Nova", "Theo", "Kai", "Rin", "Jules Park"};
        const int w[] = {42, 37, 25, 19, 11, 6}, m[] = {80, 71, 60, 45, 30, 22};
        for (int i = 0; i < 6; ++i) {
            okay::PlayerStats s; s.userId = std::string("demo-") + u[i]; s.username = u[i]; s.display = d[i]; s.rank = i + 1;
            s.wins = w[i]; s.matches = m[i]; s.totalScore = w[i] * 230 + m[i] * 40; s.bestScore = 60 + w[i]; s.seconds = m[i] * 540;
            board.push_back(s);
        }
        mine = board[2]; haveMine = true; current = true;
    } else if (signedIn && !game.empty()) {
        if (g_mrMutex) {
            okay::thr::LockMutex(g_mrMutex);
            current = g_mrBoardKey == key;
            if (current) { board = g_mrBoard; mine = g_mrMine; haveMine = g_mrHaveMine; supported = g_mrBoardSupported; }
            okay::thr::UnlockMutex(g_mrMutex);
        }
        if ((reload || !current) && !g_mrBusy) MrQueue(1, game, stat);
    }

    if (haveMine) {   // my line
        ImGui::Dummy(ImVec2(0, 6));
        const ImVec4 cardBg = okaytheme::kRaised;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
        ImGui::BeginChild("##lbmine", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
        ImGui::SetWindowFontScale(0.82f);
        ImGui::TextColored(okaytheme::Current().success, "YOUR STATS");
        ImGui::SetWindowFontScale(1.0f);
        char line[256];
        std::snprintf(line, sizeof(line), "%d wins  \xC2\xB7  %d matches  \xC2\xB7  %ld total score  \xC2\xB7  best %d  \xC2\xB7  %s played",
                      mine.wins, mine.matches, mine.totalScore, mine.bestScore, MrDuration(mine.seconds).c_str());
        ImGui::TextUnformatted(line);
        if (mine.rank > 0) { ImGui::SameLine(ImGui::GetContentRegionMax().x - 120); ImGui::TextColored(okaytheme::Current().warning, "Rank #%d", mine.rank); }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 6));
    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    const ImVec2 tblPos = ImGui::GetCursorScreenPos();
    const ImVec2 tblSize(ImGui::GetContentRegionAvail().x, std::max(120.0f, ImGui::GetContentRegionAvail().y - 4));
    const std::string me = MyUsername();
    if (ImGui::BeginTable("##lbtable", 7, tf, tblSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Wins", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Matches", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Best", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)board.size(); ++i) {
            const okay::PlayerStats& e = board[i];
            ImGui::TableNextRow(0, 34.0f);
            ImGui::PushID(i);
            ImGui::TableSetColumnIndex(0); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7);
            const ImVec4 medal = e.rank == 1 ? okaytheme::WarningFill() : e.rank == 2 ? okaytheme::kText
                               : e.rank == 3 ? okaytheme::WarningFill() : okaytheme::kTextDim;
            ImGui::TextColored(medal, "%d", e.rank);
            ImGui::TableSetColumnIndex(1);
            const bool isMe = !me.empty() && SameTitle(e.username, me);
            DrawAvatar(e.avatar, e.Name(), 26.0f);
            ImGui::SameLine(0, 8); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            const std::string label = e.Name() + (isMe ? "  (you)" : "") + "##lbrow";
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap) && !e.userId.empty())
                OpenPublicProfile(e.userId, e.username, e.Name(), e.avatar, 0);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open %s's profile", e.Name().c_str());
            auto cell = [&](int col, const std::string& v, bool hi) {
                ImGui::TableSetColumnIndex(col); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7);
                if (hi) ImGui::TextColored(okaytheme::kText, "%s", v.c_str()); else ImGui::TextDisabled("%s", v.c_str());
            };
            cell(2, std::to_string(e.wins), stat == "wins");
            cell(3, std::to_string(e.matches), stat == "matches");
            cell(4, std::to_string(e.totalScore), stat == "score");
            cell(5, std::to_string(e.bestScore), stat == "best");
            cell(6, MrDuration(e.seconds), stat == "time");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (board.empty()) {
        const char* msg = !signedIn ? "Sign in to see leaderboards."
                        : game.empty() ? "Add a game to your library to see its leaderboard."
                        : !supported ? "Leaderboards aren't set up on the server yet (docs/sql/matches.sql)."
                        : (g_mrBusy || !current) ? "Loading\xE2\x80\xA6"
                        : "No results yet. Matches show up here when a game reports them (net_match_end).";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::GetWindowDrawList()->AddText(ImVec2(tblPos.x + (tblSize.x - ts.x) * 0.5f, tblPos.y + (tblSize.y - ts.y) * 0.5f),
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
    }
}

void DrawMultiplayerPage(bool signedIn) {
    static int mpTab = 0, shownTab = -1, sel = -1;
    static char search[96] = "";
    static int gameFilter = 0;   // 0 = all, else index+1 into the game list
    static bool hideFull = false, hideEmpty = false, hidePw = false;
    if (const char* t = std::getenv("OKAY_MP_DEMO_TAB")) { static bool once = false; if (!once) { once = true; mpTab = std::atoi(t); } }   // dev screenshot hook
    const ImU32 cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImVec4 cardBg = okaytheme::kRaised;

    // Title + actions.
    {
        ImGui::Dummy(ImVec2(0, 2));
        const ImVec2 tp = ImGui::GetCursorScreenPos();
        ImGui::SetWindowFontScale(1.34f);
        ImGui::TextColored(okaytheme::kText, "Multiplayer");
        const ImVec2 tsz = ImGui::GetItemRectSize();
        ImGui::SetWindowFontScale(1.0f);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(tp.x, tp.y + tsz.y + 5), ImVec2(tp.x + tsz.x, tp.y + tsz.y + 7.5f), ImGui::GetColorU32(kAccent), 1.5f);
        const float right = ImGui::GetContentRegionMax().x;
        ImGui::SameLine(right - 566);
    {   // Find match (matchmaking queue) + Quick Play (best open server).
        static int fmPlayers = 2, fmGame = 0;
        static char fmMode[40] = "", fmRegion[24] = "";
        const auto lib = MpLibrary();
        ImGui::BeginDisabled(lib.empty() || !signedIn);
        if (ImGui::Button("Find match", ImVec2(okl::FitButtonW("Find match", okl::UiPx(130.0f)), okl::UiPx(32.0f)))) { fmGame = gameFilter > 0 ? gameFilter - 1 : 0; ImGui::OpenPopup("##findmatch"); }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", !signedIn ? "Sign in to use matchmaking." : lib.empty() ? "Build a game first: only exported games can be played online."
                                                : "Get put in a match with other players (your party comes with you).");
        if (ImGui::BeginPopup("##findmatch")) {
            ImGui::TextUnformatted("Find a match");
            ImGui::TextDisabled("Players with a similar record are grouped together.");
            ImGui::Dummy(ImVec2(0, 4));
            if (fmGame >= (int)lib.size()) fmGame = 0;
            ImGui::SetNextItemWidth(220);
            if (BeginSlateCombo("Game", lib.empty() ? "" : lib[fmGame].title.c_str())) {
                for (int i = 0; i < (int)lib.size(); ++i) if (ImGui::Selectable(lib[i].title.c_str(), fmGame == i)) fmGame = i;
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(220); ImGui::SliderInt("Players", &fmPlayers, 2, 16);
            ImGui::SetNextItemWidth(220); ImGui::InputTextWithHint("Mode", "(the game's default)", fmMode, sizeof(fmMode));
            ImGui::SetNextItemWidth(220); ImGui::InputTextWithHint("Region", "any", fmRegion, sizeof(fmRegion));
            ImGui::Dummy(ImVec2(0, 4));
            if (PrimaryButton("Find match", ImVec2(okl::FitButtonW("Find match", okl::UiPx(150.0f)), okl::UiPx(32.0f))) && !lib.empty()) { MpFindMatch(lib[fmGame].title, fmMode, fmPlayers, fmRegion); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(okl::FitButtonW("Cancel", okl::UiPx(100.0f)), okl::UiPx(32.0f)))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        PumpQuickPlay();
        ImGui::SameLine();
        ImGui::BeginDisabled(g_mpQuickBusy);
        if (PrimaryButton(g_mpQuickBusy ? "Finding\xE2\x80\xA6" : "Quick Play", ImVec2(130, 32)))
            MpQuickPlay(gameFilter > 0 ? lib[gameFilter - 1].title : std::string());
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", gameFilter > 0 ? ("Join the best open " + lib[gameFilter - 1].title + " server").c_str()
                                                   : "Join the best open server for any game in your library");
    }
        ImGui::SameLine();
        if (ImGui::Button("Connect to server\xE2\x80\xA6", ImVec2(okl::FitButtonW("Connect to server\xE2\x80\xA6", okl::UiPx(170.0f)), okl::UiPx(32.0f)))) g_mpConnectReq = true;
        ImGui::SameLine();
        ImGui::BeginDisabled(g_mpBusy);
        if (ImGui::Button(g_mpBusy ? "Refreshing\xE2\x80\xA6" : "Refresh", ImVec2(110, 32))) shownTab = -1;
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextDisabled("Join friends, find servers and invite people into your game.");
        ImGui::Dummy(ImVec2(0, 6));
    }

    if (signedIn) { DrawPartyCard(); ImGui::Dummy(ImVec2(0, 8)); }

    // Now playing: the game I'm in, and inviting friends to it.
    const MyGame cur = g_myGame;
    if (cur.running) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
        ImGui::BeginChild("##mpnow", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float tile = 62.0f;
        dl->AddRectFilled(p, ImVec2(p.x + tile, p.y + tile), ImGui::GetColorU32(kAccent), 12.0f);
        DrawGamepadIcon(dl, ImVec2(p.x + tile * 0.5f, p.y + tile * 0.5f), tile * 0.62f, okaytheme::OnColorU32(0.941f));
        ImGui::SetCursorScreenPos(ImVec2(p.x + tile + 14, p.y - 1));
        ImGui::BeginGroup();
        ImGui::SetWindowFontScale(0.82f);
        ImGui::TextColored(okaytheme::Current().success, "NOW PLAYING");
        ImGui::SetWindowFontScale(1.15f);
        ImGui::TextUnformatted(cur.title.c_str());
        ImGui::SetWindowFontScale(1.0f);
        std::string st = cur.status.empty() ? std::string("Starting\xE2\x80\xA6") : cur.status;
        if (cur.maxPlayers > 0) st += "  \xC2\xB7  " + std::to_string(cur.players) + "/" + std::to_string(cur.maxPlayers) + " players";
        ImGui::TextDisabled("%s", st.c_str());
        ImGui::EndGroup();
        const float bx = ImGui::GetContentRegionMax().x - 290;
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + bx, p.y + (tile - 32) * 0.5f));
        const bool can = !cur.invite.empty();
        ImGui::BeginDisabled(!can);
        if (PrimaryButton("Invite friends", ImVec2(okl::FitButtonW("Invite friends", okl::UiPx(150.0f)), okl::UiPx(32.0f)))) { g_inviteMode = 0; g_inviteOpenReq = true; }
        ImGui::SameLine();
        if (ImGui::Button("Copy join link", ImVec2(okl::FitButtonW("Copy join link", okl::UiPx(130.0f)), okl::UiPx(32.0f)))) { ImGui::SetClipboardText(cur.invite.c_str()); Toast("Join link copied", 1); }
        ImGui::EndDisabled();
        if (!can && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Host or join a match in the game first.");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + tile + 10));
        if (!can) ImGui::TextDisabled("Host or join a match in the game and friends can join you from here.");
        else ImGui::TextDisabled("Friends see \"Join game\" on you in their Friends list. In the game, Shift+Tab opens Invite friends.");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8));
    }

    // Tabs.
    MpLoadSaved();
    const auto friendsNow = MpFriends();
    int friendGames = 0;
    for (const auto& f : friendsNow) { okay::JoinLink l; if (f.status == "accepted" && f.online && okay::JoinLink::Parse(f.sessionCode, l)) ++friendGames; }
    const char* tabNames[] = {"Internet", "Friends", "LAN", "Favorites", "History", "Recent players", "Leaderboards"};
    const int tabCounts[] = {-1, friendGames, -1, (int)g_mpFavs.size(), (int)g_mpHist.size(), -1, -1};
    for (int i = 0; i < 7; ++i) {
        if (i) ImGui::SameLine(0, 6);
        char lbl[48];
        if (tabCounts[i] > 0) std::snprintf(lbl, sizeof(lbl), "%s  %d##mpt%d", tabNames[i], tabCounts[i], i);
        else std::snprintf(lbl, sizeof(lbl), "%s##mpt%d", tabNames[i], i);
        const bool on = mpTab == i;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? kAccent : okaytheme::InkV(0.05f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? kAccent : okaytheme::InkV(0.1f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
        if (ImGui::Button(lbl, ImVec2(0, 28)) && !on) { mpTab = i; shownTab = -1; sel = -1; }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }
    if (mpTab >= 5) {   // people + stats rather than servers
        const bool reload = shownTab != mpTab;
        shownTab = mpTab;
        if (mpTab == 5) DrawRecentPlayers(signedIn, reload);
        else DrawGameLeaderboards(signedIn, reload);
        return;
    }
    if (shownTab != mpTab && !g_mpBusy && !MpDemo()) { shownTab = mpTab; MpRefresh(mpTab); }

    // Filters.
    ImGui::Dummy(ImVec2(0, 4));
    const auto lib = MpLibrary();
    ImGui::SetNextItemWidth(190);
    ImGui::InputTextWithHint("##mpsearch", "Search servers", search, sizeof(search));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    if (gameFilter > (int)lib.size()) gameFilter = 0;
    if (BeginSlateCombo("##mpgf", gameFilter == 0 ? "All games" : lib[gameFilter - 1].title.c_str())) {
        if (ImGui::Selectable("All games", gameFilter == 0)) gameFilter = 0;
        for (int i = 0; i < (int)lib.size(); ++i) if (ImGui::Selectable(lib[i].title.c_str(), gameFilter == i + 1)) gameFilter = i + 1;
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 14); ImGui::Checkbox("Not full", &hideFull);
    ImGui::SameLine(0, 10); ImGui::Checkbox("Has players", &hideEmpty);
    ImGui::SameLine(0, 10); ImGui::Checkbox("No password", &hidePw);
    // Rows (demo: a fixed set so the page can be reviewed offline).
    std::vector<MpRow> rows;
    if (MpDemo()) {
        auto mk = [](const char* name, const char* game, const char* map, int pl, int mx, int ping, bool pw, bool ded, const char* host, const char* fr = "") {
            MpRow r; r.info.name = name; r.game = game; r.info.game = game; r.info.map = map; r.info.players = pl; r.info.maxPlayers = mx;
            r.info.pingMs = ping; r.info.responded = ping >= 0; r.info.password = pw; r.info.dedicated = ded;
            std::string h; std::uint16_t port = 0; okay::SplitHostPort(host, h, port); r.info.host = h; r.info.port = port; r.friendName = fr;
            return r;
        };
        if (mpTab == 0) rows = {mk("EU 1 \xC2\xB7 Ranked", "Star Arena", "Docks", 14, 16, 23, false, true, "203.0.113.7:45000"),
                                mk("US East Casual", "Star Arena", "Canyon", 6, 16, 88, false, true, "198.51.100.4:45000"),
                                mk("Kart Rush \xE2\x80\x94 Night Cup", "Kart Rush", "Neon Loop", 8, 8, 41, false, true, "203.0.113.20:45002"),
                                mk("Builders' Club", "Block Party", "Creative", 3, 24, 57, true, true, "play.example.com:45000"),
                                mk("Asia Practice", "Star Arena", "Docks", 0, 16, 182, false, true, "192.0.2.80:45000")};
        else if (mpTab == 1) rows = {mk("Maya's lobby", "Star Arena", "Docks", 2, 8, 4, false, false, "192.168.1.20:45000", "Maya Chen")};
        else if (mpTab == 2) rows = {mk("Living room", "Kart Rush", "Neon Loop", 1, 4, 1, false, false, "192.168.1.31:45000")};
    } else {
        if (g_mpMutex) { okay::thr::LockMutex(g_mpMutex); if (g_mpRowsTab == mpTab) rows = g_mpRows; okay::thr::UnlockMutex(g_mpMutex); }
    }
    std::string q = search; for (auto& c : q) c = (char)std::tolower((unsigned char)c);
    std::vector<int> view;
    for (int i = 0; i < (int)rows.size(); ++i) {
        const MpRow& r = rows[i];
        if (gameFilter > 0 && !SameTitle(r.game, lib[gameFilter - 1].title)) continue;
        if (hideFull && r.info.maxPlayers > 0 && r.info.players >= r.info.maxPlayers) continue;
        if (hideEmpty && r.info.players <= 0) continue;
        if (hidePw && r.info.password) continue;
        if (!q.empty()) {
            std::string hay = r.info.name + " " + r.game + " " + r.info.map + " " + r.friendName;
            for (auto& c : hay) c = (char)std::tolower((unsigned char)c);
            if (hay.find(q) == std::string::npos) continue;
        }
        view.push_back(i);
    }

    auto rowLink = [&](const MpRow& r) {
        okay::JoinLink l;
        if (!r.link.empty() && okay::JoinLink::Parse(r.link, l)) return l;
        l.game = r.game.empty() && gameFilter > 0 ? lib[gameFilter - 1].title : r.game;
        l.addr = r.info.Address(); l.name = r.info.name; l.needsPassword = r.info.password;
        return l;
    };
    auto joinRow = [&](const MpRow& r) { MpJoin(rowLink(r).Format(), r.friendName); };

    // The table.
    ImGui::Dummy(ImVec2(0, 4));
    const float footH = 64.0f;
    const ImGuiTableFlags tf = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    const ImVec2 tblPos = ImGui::GetCursorScreenPos();
    const ImVec2 tblSize(ImGui::GetContentRegionAvail().x, std::max(120.0f, ImGui::GetContentRegionAvail().y - footH));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    if (ImGui::BeginTable("##mpservers", 6, tf, tblSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoResize, 22.0f);
        ImGui::TableSetupColumn("Server", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80.0f);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 64.0f);
        ImGui::TableHeadersRow();
        if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) if (ss->SpecsCount > 0) {
            const int col = ss->Specs[0].ColumnIndex; const bool asc = ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            std::stable_sort(view.begin(), view.end(), [&](int a, int b) {
                const MpRow& x = rows[a]; const MpRow& y = rows[b];
                int c = 0;
                if (col == 1) c = x.info.name.compare(y.info.name);
                else if (col == 2) c = x.game.compare(y.game);
                else if (col == 3) c = x.info.players - y.info.players;
                else if (col == 4) c = x.info.map.compare(y.info.map);
                else { const int px = x.info.pingMs < 0 ? 99999 : x.info.pingMs, py = y.info.pingMs < 0 ? 99999 : y.info.pingMs; c = px - py; }
                return asc ? c < 0 : c > 0;
            });
        }
        for (int vi : view) {
            const MpRow& r = rows[vi];
            ImGui::TableNextRow(0, 30.0f);
            ImGui::PushID(vi);
            ImGui::TableSetColumnIndex(0);
            if (r.info.password) {   // padlock
                ImDrawList* d = ImGui::GetWindowDrawList(); const ImVec2 c = ImGui::GetCursorScreenPos();
                const ImU32 lc = okaytheme::WarningFillU32();
                d->AddRectFilled(ImVec2(c.x + 5, c.y + 9), ImVec2(c.x + 15, c.y + 17), lc, 2.0f);
                d->PathArcTo(ImVec2(c.x + 10, c.y + 9), 3.4f, 3.14159f, 6.28318f, 10); d->PathStroke(lc, 0, 1.6f);
            }
            ImGui::TableSetColumnIndex(1);
            const bool selected = sel == vi;
            const std::string label = (r.info.name.empty() ? r.info.Address() : r.info.name) + "##mprow";
            const float nameX = ImGui::GetCursorScreenPos().x;
            if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, 26))) {
                sel = vi;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) joinRow(r);
            }
            if (ImGui::BeginPopupContextItem("##mprowctx")) {
                sel = vi;
                if (ImGui::MenuItem("Join")) joinRow(r);
                if (!r.relayed && !r.info.host.empty()) {
                    const bool fav = MpIsFav(r.info.Address());
                    if (ImGui::MenuItem(fav ? "Remove from favorites" : "Add to favorites")) MpToggleFav(r.info.Address(), r.game, r.info.name);
                    if (ImGui::MenuItem("Copy address")) { ImGui::SetClipboardText(r.info.Address().c_str()); Toast("Address copied", 1); }
                }
                if (ImGui::MenuItem("Server info\xE2\x80\xA6")) MpOpenInfo(r);
                if (ImGui::MenuItem("Copy join link")) { ImGui::SetClipboardText(rowLink(r).Format().c_str()); Toast("Join link copied", 1); }
                ImGui::EndPopup();
            }
            {   // tags after the name: whose game it is / a dedicated server
                const float nameW = ImGui::CalcTextSize(label.c_str(), nullptr, true).x;
                float tx = nameX + nameW + 10;
                const float ty = ImGui::GetItemRectMin().y + (ImGui::GetItemRectSize().y - ImGui::GetTextLineHeight()) * 0.5f;
                ImDrawList* d = ImGui::GetWindowDrawList();
                if (!r.friendName.empty()) { d->AddText(ImVec2(tx, ty), okaytheme::SuccessU32(), r.friendName.c_str()); tx += ImGui::CalcTextSize(r.friendName.c_str()).x + 10; }
                if (r.info.dedicated) d->AddText(ImVec2(tx, ty), cDim, "dedicated");
            }
            ImGui::TableSetColumnIndex(2); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            ImGui::TextUnformatted(r.game.empty() ? "\xE2\x80\x94" : r.game.c_str());
            ImGui::TableSetColumnIndex(3); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            if (r.info.maxPlayers > 0) {
                const bool full = r.info.players >= r.info.maxPlayers;
                ImGui::TextColored(full ? okaytheme::Current().danger : okaytheme::kText, "%d / %d", r.info.players, r.info.maxPlayers);
            } else ImGui::TextDisabled("\xE2\x80\x94");
            ImGui::TableSetColumnIndex(4); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            ImGui::TextUnformatted(r.info.map.empty() ? "\xE2\x80\x94" : r.info.map.c_str());
            ImGui::TableSetColumnIndex(5); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            if (r.relayed) ImGui::TextDisabled("relay");
            else if (r.info.pingMs < 0) ImGui::TextDisabled(g_mpBusy ? "\xE2\x80\xA6" : "\xE2\x80\x94");
            else {
                const int pm = r.info.pingMs;
                ImGui::TextColored(pm < 60 ? okaytheme::Current().success : pm < 130 ? okaytheme::Current().warning : okaytheme::Current().danger, "%d", pm);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    if (view.empty()) {   // empty states, drawn over the table body
        const char* msg = g_mpBusy ? "Looking for servers\xE2\x80\xA6"
                        : !rows.empty() ? "No servers match your filters."
                        : mpTab == 0 ? (signedIn ? "No public servers are online right now.\nHost one with:  okay-player --server --advertise --public-addr <your address>"
                                                 : "Sign in to see public servers.")
                        : mpTab == 1 ? "None of your friends are in a joinable game right now."
                        : mpTab == 2 ? "No games found on your local network."
                        : mpTab == 3 ? "Right-click a server and choose Add to favorites."
                                     : "Servers you join show up here.";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::GetWindowDrawList()->AddText(ImVec2(tblPos.x + (tblSize.x - ts.x) * 0.5f, tblPos.y + (tblSize.y - ts.y) * 0.5f), cDim, msg);
    }

    // Footer: the selected server + Join.
    ImGui::Dummy(ImVec2(0, 6));
    const MpRow* sr = (sel >= 0 && sel < (int)rows.size()) ? &rows[sel] : nullptr;
    if (sr) {
        ImGui::BeginGroup();
        ImGui::TextUnformatted(sr->info.name.empty() ? sr->info.Address().c_str() : sr->info.name.c_str());
        std::string det = sr->relayed ? std::string("Through a relay") : sr->info.Address();
        if (!sr->region.empty()) det += "  \xC2\xB7  " + sr->region;
        if (sr->info.dedicated) det += "  \xC2\xB7  Dedicated";
        if (sr->info.auth) det += "  \xC2\xB7  Sign-in required";
        if (sr->info.encrypted) det += "  \xC2\xB7  Encrypted";
        if (sr->info.password) det += "  \xC2\xB7  Password";
        ImGui::TextDisabled("%s", det.c_str());
        ImGui::EndGroup();
        const float right = ImGui::GetContentRegionMax().x;
        ImGui::SameLine(right - 410);
        if (ImGui::Button("Players", ImVec2(okl::FitButtonW("Players", okl::UiPx(110.0f)), okl::UiPx(36.0f)))) MpOpenInfo(*sr);
        if (!sr->relayed && !sr->info.host.empty()) {
            ImGui::SameLine(right - 290);
            const bool fav = MpIsFav(sr->info.Address());
            if (ImGui::Button(fav ? "Unfavorite" : "Favorite", ImVec2(110, 36))) MpToggleFav(sr->info.Address(), sr->game, sr->info.name);
        }
        ImGui::SameLine(right - 170);
        const bool full = sr->info.maxPlayers > 0 && sr->info.players >= sr->info.maxPlayers;
        if (PrimaryButton(full ? "Join (full)" : "Join", ImVec2(170, 36))) joinRow(*sr);
    } else {
        ImGui::TextDisabled("Double-click a server to join it. Right-click for players, favorites and links.");
    }
    DrawMpInfoPopup(joinRow);
}
} // namespace okl
