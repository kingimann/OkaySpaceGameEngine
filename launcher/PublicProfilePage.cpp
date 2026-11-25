// The public profile page (PublicProfilePage.hpp): someone's profile with its banner, showcase, achievements, friends
// and activity, plus the sample profile for demo runs. Moved out of launcher/main.cpp unchanged
// (docs/engine/modularization_plan.md, Part C section 21, batch L6).
#include "LauncherShared.hpp"
#include "PublicProfilePage.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Profiles.hpp"
#include "okay/Platform/Achievements.hpp"   // playtime, achievements
#include "OkayTheme.hpp"
#include "AdminPage.hpp"         // OpenReport
#include "CoverArtAvatars.hpp"   // AvatarTexture, DrawAvatar
#include "NavIcons.hpp"          // DrawNavIcon, RoundIconButton

namespace okl {

// ============================================================================
// Public profiles (Steam-style). A full page that can show ANY player — me, a
// friend, a server member or a forum author. It overlays the current tab's
// content, so Back simply returns there. All network work runs on a worker
// (ProfileWorker); OKAY_UI_DEMO_USER seeds sample profiles for screenshots.
// Server side: docs/sql/social.sql + the optional docs/sql/profile_showcase.sql
// (without it, others' showcases are hidden and mine is kept on this device).
// ============================================================================
struct PfOpenReq { std::string uid, username, name, avatar; int accent = 0; };
bool g_pfOpen = false;                  // the profile page is showing
static PfOpenReq g_pfReq;                      // who: uid preferred, else username
static unsigned g_pfSerial = 0;                // bumps per open (drops stale results)
static okay::thr::Mutex* g_pfMutex = nullptr;
static volatile bool g_pfBusy = false;         // a profile job is in flight
static bool g_pfReady = false;                 // (guarded) a fresh bundle is waiting
static unsigned g_pfReadySerial = 0;           // (guarded) which open it belongs to
static okay::ProfileBundle g_pfResult;         // (guarded)
static int g_pfActType = 0, g_pfActResult = 0; // (guarded) last action: type, 1 ok / -1 failed

void OpenPublicProfile(const std::string& uid, const std::string& username,
                              const std::string& name, const std::string& avatar,
                              int accent) {
    if (uid.empty() && username.empty()) return;
    if (!g_pfMutex) g_pfMutex = okay::thr::CreateMutex();
    g_pfReq.uid = uid; g_pfReq.username = username; g_pfReq.name = name;
    g_pfReq.avatar = avatar; g_pfReq.accent = accent;
    g_pfOpen = true; ++g_pfSerial;
    if (g_chatInOwnWindow) g_raiseMainReq = true;   // the profile shows in the launcher
}
void ClosePublicProfile() { g_pfOpen = false; }

// type: 0 load, 1 save my showcase, 2 add friend, 3 accept, 4 remove/decline,
// 5 follow, 6 unfollow.
// Every job ends with a fresh Load() so the page shows the server's truth.
struct PfJob {
    int type = 0; unsigned serial = 0;
    std::string target; bool byName = false;   // whose profile to (re)load
    bool syncLocal = false;                    // mirror my local achievements first
    std::vector<std::string> ach; std::vector<long> games; int banner = -1;
    long row = 0;
};
static int ProfileWorker(void* arg) {
    PfJob* j = (PfJob*)arg;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Profiles pr(a);
        int act = 0;
        if (j->type == 1) {
            pr.SyncAchievements(okay::Achievements::All());
            act = pr.SaveShowcase(j->ach, j->games, j->banner) ? 1 : -1;
        } else if (j->type == 2) act = pr.AddFriend(j->target) ? 1 : -1;
        else if (j->type == 3)   act = pr.Accept(j->row) ? 1 : -1;
        else if (j->type == 4)   act = pr.Remove(j->row) ? 1 : -1;
        else if (j->type == 5)   act = pr.Follow(j->target) ? 1 : -1;
        else if (j->type == 6)   act = pr.Unfollow(j->target) ? 1 : -1;
        else if (j->syncLocal)   pr.SyncAchievements(okay::Achievements::All());
        okay::ProfileBundle b = pr.Load(j->target, j->byName);
        if (j->type == 0 && !j->syncLocal && b.isMe && pr.SyncAchievements(okay::Achievements::All()))
            b = pr.Load(j->target, j->byName);
        if (g_pfMutex) okay::thr::LockMutex(g_pfMutex);
        g_pfResult = std::move(b); g_pfReadySerial = j->serial; g_pfReady = true;
        if (j->type) { g_pfActType = j->type; g_pfActResult = act; }
        if (g_pfMutex) okay::thr::UnlockMutex(g_pfMutex);
    } catch (...) {}
    g_pfBusy = false;
    delete j;
    return 0;
}

// My showcase picks on this device: the fallback when the server doesn't have
// the showcase columns yet (and a cache of my last save otherwise).
static std::string PfLocalShowcasePath() { return okay::Achievements::Dir() + "/profile_showcase.txt"; }
PfLocalShowcase LoadLocalShowcase() {
    PfLocalShowcase s;
    std::ifstream f(PfLocalShowcasePath());
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("ach=", 0) == 0 && line.size() > 4) s.ach.push_back(line.substr(4));
        else if (line.rfind("game=", 0) == 0) { long id = std::atol(line.c_str() + 5); if (id > 0) s.games.push_back(id); }
        else if (line.rfind("banner=", 0) == 0) s.banner = std::atoi(line.c_str() + 7);
    }
    return s;
}
void SaveLocalShowcase(const PfLocalShowcase& s) {
    std::ofstream f(PfLocalShowcasePath(), std::ios::trunc);
    if (!f) return;
    for (const auto& a : s.ach) f << "ach=" << a << "\n";
    for (long g : s.games) f << "game=" << g << "\n";
    f << "banner=" << s.banner << "\n";
}

static const SocialPlatform* SocialFor(const std::string& key) {
    for (const auto& sp : kSocials) if (key == sp.key) return &sp;
    return nullptr;
}
// What a user typed -> what we store: a bare handle (no @, no URL), or a full
// https URL for the website. Empty = invalid.
std::string CleanSocial(const std::string& key, std::string v) {
    while (!v.empty() && std::isspace((unsigned char)v.back())) v.pop_back();
    while (!v.empty() && std::isspace((unsigned char)v.front())) v.erase(0, 1);
    if (v.empty()) return v;
    if (key == "website") {
        if (v.rfind("http://", 0) != 0 && v.rfind("https://", 0) != 0) v = "https://" + v;
        return ShellSafeUrl(v) ? v.substr(0, 200) : std::string();
    }
    if (const SocialPlatform* sp = SocialFor(key); sp && sp->prefix[0]) {   // accept a pasted profile URL too
        std::string host = std::string(sp->prefix).substr(8);                 // after "https://"
        host = host.substr(0, host.find('/'));
        if (auto at = v.find(host); at != std::string::npos) {
            v = v.substr(at + host.size());
            while (!v.empty() && (v[0] == '/' || v[0] == '@')) v.erase(0, 1);
            v = v.substr(0, v.find_first_of("/?#"));
        }
    }
    while (!v.empty() && v[0] == '@') v.erase(0, 1);
    std::string out;
    for (char c : v) if (std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-' || (key == "discord" && c == '#')) out += c;
    return out.substr(0, 60);
}
static std::string SocialUrl(const std::string& key, const std::string& v) {
    const SocialPlatform* sp = SocialFor(key);
    if (!sp || v.empty()) return std::string();
    if (key == "website") return ShellSafeUrl(v) ? v : std::string();
    if (!sp->prefix[0]) return std::string();
    const std::string u = sp->prefix + v;
    return ShellSafeUrl(u) ? u : std::string();
}

// The blue "verified" rosette with a white check.
void DrawVerifiedBadge(ImDrawList* d, ImVec2 c, float r) {
    const int n = 16;
    for (int i = 0; i < n; ++i) {   // scalloped edge
        const float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
        const float r0 = (i & 1) ? r * 0.86f : r, r1 = (i & 1) ? r : r * 0.86f;
        d->AddTriangleFilled(c, ImVec2(c.x + std::cos(a0) * r0, c.y + std::sin(a0) * r0),
                             ImVec2(c.x + std::cos(a1) * r1, c.y + std::sin(a1) * r1), IM_COL32(56, 151, 240, 255));
    }
    d->AddCircleFilled(c, r * 0.80f, IM_COL32(56, 151, 240, 255), 20);
    d->AddLine(ImVec2(c.x - r * 0.38f, c.y + r * 0.02f), ImVec2(c.x - r * 0.10f, c.y + r * 0.32f), IM_COL32_WHITE, std::max(1.4f, r * 0.20f));
    d->AddLine(ImVec2(c.x - r * 0.10f, c.y + r * 0.32f), ImVec2(c.x + r * 0.42f, c.y - r * 0.30f), IM_COL32_WHITE, std::max(1.4f, r * 0.20f));
}

// A badge on a profile: earned from what you've done, or awarded by staff.
struct ProfileBadge { std::string name, desc; ImU32 color; char glyph; };
static std::vector<ProfileBadge> ProfileBadges(const okay::ProfileBundle& b, int achN, int points, int gameN, long playSecs, long editorSecs) {
    std::vector<ProfileBadge> out;
    for (const auto& s : b.badges) {   // staff-awarded first
        if (s == "staff")     out.push_back({"Staff", "Works on OkaySpace.", IM_COL32(237, 66, 69, 255), 'S'});
        if (s == "developer") out.push_back({"Developer", "Builds the engine.", IM_COL32(150, 110, 255, 255), 'D'});
        if (s == "moderator") out.push_back({"Moderator", "Keeps the community friendly.", IM_COL32(67, 181, 129, 255), 'M'});
        if (s == "beta")      out.push_back({"Beta Tester", "Tested OkaySpace before release.", IM_COL32(245, 150, 60, 255), 'B'});
        if (s == "partner")   out.push_back({"Partner", "An official OkaySpace partner.", IM_COL32(250, 200, 70, 255), 'P'});
    }
    const long now = (long)std::time(nullptr);
    if (b.p.memberSince > 0 && b.p.memberSince < 1767225600L)   // joined before 2026
        out.push_back({"Early Adopter", "Joined OkaySpace in its first year.", IM_COL32(90, 200, 230, 255), 'E'});
    if (b.p.memberSince > 0 && now - b.p.memberSince > 365L * 86400)
        out.push_back({"Veteran", "Member for over a year.", IM_COL32(190, 160, 110, 255), 'V'});
    if (gameN >= 5) out.push_back({"Prolific Creator", "Published 5+ games.", IM_COL32(255, 120, 90, 255), 'C'});
    else if (gameN >= 1) out.push_back({"Creator", "Published a game.", IM_COL32(255, 150, 110, 255), 'C'});
    if (achN >= 10) out.push_back({"Achievement Hunter", "Unlocked 10+ achievements.", IM_COL32(250, 200, 70, 255), 'A'});
    if (points >= 500) out.push_back({"Point Collector", "Earned 500+ achievement points.", IM_COL32(255, 220, 120, 255), '*'});
    if (b.friends >= 10) out.push_back({"Social Butterfly", "10+ friends.", IM_COL32(240, 110, 180, 255), 'F'});
    if (playSecs >= 100L * 3600) out.push_back({"Dedicated Player", "100+ hours played.", IM_COL32(100, 170, 255, 255), 'H'});
    if (editorSecs >= 50L * 3600) out.push_back({"Builder", "50+ hours in the editor.", IM_COL32(130, 210, 120, 255), 'E'});
    return out;
}
static std::string HoursText(long secs) {
    char b[32];
    if (secs < 3600) std::snprintf(b, sizeof(b), "%ld min", std::max(1L, secs / 60));
    else if (secs < 36000) std::snprintf(b, sizeof(b), "%.1f h", secs / 3600.0);
    else std::snprintf(b, sizeof(b), "%ld h", secs / 3600);
    return b;
}

static okay::ProfileBundle DemoProfileBundle(const PfOpenReq& r, const std::string& me) {
    okay::ProfileBundle b;
    b.found = b.reached = b.showcaseSupported = b.achSupported = true;
    b.followSupported = true; b.followers = 128; b.following = 37; b.followsMe = true;
    const long t0 = (long)std::time(nullptr), day = 86400;
    okay::PublicProfile& p = b.p;
    auto ach = [&](const char* id, const char* label, long ago) {
        okay::Achievement a; a.id = id; a.label = label; a.when = t0 - ago; b.achievements.push_back(a); };
    auto game = [&](long id, const char* t, const char* d, const char* k, long dls) {
        okay::WorkshopItem w; w.id = id; w.title = t; w.description = d; w.kind = k; w.downloads = dls; b.games.push_back(w); };
    auto ev = [&](const char* k, const char* d, long ago) {
        okay::FriendEvent e; e.kind = k; e.detail = d; e.when = t0 - ago; b.activity.push_back(e); };
    const std::string key = !r.username.empty() ? r.username : r.uid;
    if (key == me || r.uid == "me") {
        p.id = "me"; p.username = me; p.display = me; p.accent = 3; p.bannerAccent = -1;
        p.bio = "Indie dev making small, loud arcade games in OkaySpace. Neon Drift is my baby; "
                "Pixel Farm is the cozy one. Always up for a jam team.";
        p.location = "Pixel City"; p.favoriteGame = "Neon Drift"; p.customStatus = "shipping Neon Drift 2.0";
        p.online = true; p.playing = "In the launcher"; p.memberSince = t0 - 412 * day;
        ach("perfect_lap", "Perfect Lap", 2 * 3600); ach("speed_demon", "Speed Demon", 1 * day);
        ach("first_publish", "Published!", 3 * day); ach("coin_hoarder", "Coin Hoarder", 6 * day);
        ach("night_owl", "Night Owl", 9 * day); ach("untouchable", "Untouchable", 14 * day);
        ach("jam_survivor", "Jam Survivor", 21 * day); ach("explorer", "Explorer", 33 * day);
        ach("social_butterfly", "Social Butterfly", 40 * day); ach("master_builder", "Master Builder", 58 * day);
        ach("marathon", "Marathon Runner", 77 * day); ach("first_win", "First Victory", 120 * day);
        p.showcaseAch = {"untouchable", "perfect_lap", "jam_survivor", "speed_demon", "master_builder", "first_win"};
        game(41, "Neon Drift", "Synthwave arcade racer with drift combos and a ghost-lap leaderboard.", "game", 4210);
        game(57, "Pixel Farm", "A cozy farming loop: plant, water, sell, repeat. Seasons included.", "game", 2380);
        game(33, "Asteroid Belt", "Classic asteroids with a gravity-well twist.", "game", 1190);
        game(62, "Dungeon Dash", "One-room roguelike dungeon runs in under 3 minutes.", "game", 640);
        game(70, "Racer Starter Kit", "Template: car physics, laps and a HUD to start your own racer.", "template", 312);
        p.showcaseGames = {41, 57, 62, 33};
        b.friends = 23; b.isMe = true;
        b.extrasSupported = true; p.verified = true;
        p.socials = {{"youtube", "neondriftdev"}, {"twitch", "kingimann"}, {"x", "okayspace"}, {"website", "https://okayspace.dev"}};
        b.badges = {"developer", "beta"};
        auto pt = [&](const char* k, const char* t, long h, long ago) { okay::PlaytimeEntry e; e.key = k; e.title = t; e.seconds = h * 3600 + 1200; e.lastPlayed = t0 - ago; b.playtime.push_back(e); };
        pt("editor", "OkaySpace Editor", 214, 3600); pt("game:Neon Drift", "Neon Drift", 86, 2 * 3600);
        pt("game:Dungeon Dash", "Dungeon Dash", 31, day); pt("game:Pixel Farm", "Pixel Farm", 12, 4 * day);
        pt("game:Asteroid Belt", "Asteroid Belt", 3, 20 * day);
        for (auto& a : b.achievements) a.points = (a.id == "untouchable" || a.id == "perfect_lap") ? 50 : a.id == "master_builder" ? 100 : 25;
        ev("publish", "Neon Drift 2.0", 3 * 3600); ev("achievement", "Perfect Lap", 2 * 3600 + 600);
        ev("playing", "Dungeon Dash", day); ev("achievement", "Speed Demon", day + 1800);
    } else if (key == "maya" || r.uid == "u-maya") {
        p.id = "u-maya"; p.username = "maya"; p.display = "Maya Chen"; p.accent = 2;
        p.bio = "Level designer by day, speedrunner by night. Currently holding the Neon Drift any% record (for now).";
        p.location = "Vancouver, BC"; p.favoriteGame = "Neon Drift"; p.online = true; p.playing = "Playing Neon Drift";
        p.sessionCode = "QX7-42"; p.memberSince = t0 - 290 * day;
        ach("wr_holder", "World Record", 5 * 3600); ach("perfect_lap", "Perfect Lap", 2 * day);
        ach("no_brakes", "No Brakes", 4 * day); ach("speed_demon", "Speed Demon", 9 * day);
        ach("explorer", "Explorer", 30 * day); ach("first_win", "First Victory", 200 * day);
        p.showcaseAch = {"wr_holder", "no_brakes", "perfect_lap", "speed_demon"};
        game(88, "Skyline Sprint", "Rooftop parkour time trials across a neon city.", "game", 1820);
        game(91, "Glow Garden", "A tiny relaxing light-puzzle garden.", "scene", 260);
        b.friends = 41; b.friendRow = 1; b.friendStatus = "accepted";
        ev("playing", "Neon Drift", 240); ev("achievement", "World Record", 5 * 3600); ev("publish", "Skyline Sprint", 6 * day);
    } else {
        p.username = !r.username.empty() ? r.username : (!r.name.empty() ? r.name : r.uid);
        for (auto& c : p.username) c = (char)std::tolower((unsigned char)c);
        p.id = !r.uid.empty() ? r.uid : "u-" + p.username;
        p.display = !r.name.empty() ? r.name : p.username;
        p.avatar = r.avatar;
        unsigned h = 2166136261u; for (char c : p.username) h = (h ^ (unsigned char)c) * 16777619u;   // dup-allow: moved unchanged from launcher/main.cpp (L6); one copy
        p.accent = (int)(h % 7u); p.online = (h & 1u) != 0; p.lastSeen = t0 - (long)(h % 20u + 1) * 3600;
        p.memberSince = t0 - (long)(h % 500u + 30) * day;
        p.bio = "Making games with friends on OkaySpace.";
        p.favoriteGame = (h & 2u) ? "Asteroid Belt" : "Pixel Farm";
        ach("first_win", "First Victory", 3 * day); ach("explorer", "Explorer", 12 * day);
        ach("night_owl", "Night Owl", 40 * day);
        if (h & 4u) game(120 + (long)(h % 50u), "Untitled Platformer", "A work-in-progress platformer.", "game", (long)(h % 300u));
        b.friends = (int)(h % 30u) + 2;
        if (!r.uid.empty()) { b.friendRow = 9; b.friendStatus = "accepted"; ev("online", "", 3 * 3600); }
    }
    return b;
}

// Recolor the vertices added since v0 with a linear gradient from c0 (at p0)
// to c1 (at p1); vertex alpha is multiplied by the gradient alpha. Lets a
// rounded AddRectFilled become a rounded gradient.
static void PfGradient(ImDrawList* dl, int v0, ImVec2 p0, ImVec2 p1, ImVec4 c0, ImVec4 c1) {
    ImVec2 d(p1.x - p0.x, p1.y - p0.y);
    float len2 = d.x * d.x + d.y * d.y; if (len2 <= 0.0f) len2 = 1.0f;
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        float t = ((v.pos.x - p0.x) * d.x + (v.pos.y - p0.y) * d.y) / len2;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float a = (float)((v.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        ImVec4 c(c0.x + (c1.x - c0.x) * t, c0.y + (c1.y - c0.y) * t, c0.z + (c1.z - c0.z) * t,
                 (c0.w + (c1.w - c0.w) * t) * a);
        v.col = ImGui::GetColorU32(c);
    }
}

// Achievement "icon": a hexagon medal in the achievement's own hue with a
// trophy glyph (achievements carry no artwork, so the id picks the color).
void DrawAchMedal(ImDrawList* dl, ImVec2 c, float r, const std::string& id, float alpha) {
    const float PI = 3.14159265f, s = r / 15.0f;
    ImVec4 nc = NameColor(id);
    dl->AddCircleFilled(c, r + 3.0f, ImGui::GetColorU32(ImVec4(nc.x, nc.y, nc.z, 0.14f * alpha)), 32);
    int v0 = dl->VtxBuffer.Size;
    dl->AddNgonFilled(c, r, IM_COL32_WHITE, 6);
    PfGradient(dl, v0, ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r),
               ImVec4(nc.x * 0.62f, nc.y * 0.62f, nc.z * 0.62f, alpha), ImVec4(nc.x * 0.30f, nc.y * 0.30f, nc.z * 0.30f, alpha));
    dl->AddNgon(c, r, ImGui::GetColorU32(ImVec4(nc.x, nc.y, nc.z, alpha)), 6, 1.8f);
    ImU32 g = ImGui::GetColorU32(ImVec4(nc.x * 0.30f + 0.70f, nc.y * 0.30f + 0.70f, nc.z * 0.30f + 0.70f, alpha));
    dl->PathLineTo(ImVec2(c.x - 5.6f * s, c.y - 7.5f * s));
    dl->PathLineTo(ImVec2(c.x + 5.6f * s, c.y - 7.5f * s));
    dl->PathArcTo(ImVec2(c.x, c.y - 2.6f * s), 5.6f * s, 0.0f, PI, 14);
    dl->PathFillConvex(g);
    dl->PathArcTo(ImVec2(c.x - 5.6f * s, c.y - 4.6f * s), 2.6f * s, PI * 0.5f, PI * 1.5f, 8); dl->PathStroke(g, 0, 1.5f * s);
    dl->PathArcTo(ImVec2(c.x + 5.6f * s, c.y - 4.6f * s), 2.6f * s, -PI * 0.5f, PI * 0.5f, 8); dl->PathStroke(g, 0, 1.5f * s);
    dl->AddRectFilled(ImVec2(c.x - 1.2f * s, c.y + 2.6f * s), ImVec2(c.x + 1.2f * s, c.y + 6.0f * s), g);
    dl->AddRectFilled(ImVec2(c.x - 4.8f * s, c.y + 5.8f * s), ImVec2(c.x + 4.8f * s, c.y + 8.4f * s), g, 1.2f * s);
}

// "first_win" -> "First Win" (label for showcase ids we have no row for).
std::string PfPrettyId(const std::string& id) {
    std::string o; bool up = true;
    for (char c : id) {
        if (c == '_' || c == '-') { o += ' '; up = true; continue; }
        o += up ? (char)std::toupper((unsigned char)c) : c; up = false;
    }
    return o;
}
static std::string PfCount(long n) {   // 4210 -> "4.2k"
    char b[32];
    if (n >= 1000000) std::snprintf(b, sizeof(b), "%.1fM", n / 1e6);
    else if (n >= 10000) std::snprintf(b, sizeof(b), "%ldk", n / 1000);
    else if (n >= 1000) std::snprintf(b, sizeof(b), "%.1fk", n / 1e3);
    else std::snprintf(b, sizeof(b), "%ld", n);
    return b;
}


PfPageOut DrawPublicProfilePage(okay::account::AccountService& account, int fromTab) {
    PfPageOut out;
    static const char* kDemoUser = std::getenv("OKAY_UI_DEMO_USER");   // dev screenshot hook
    const bool kDemo = kDemoUser != nullptr;
    const std::string meName = kDemo ? std::string(kDemoUser) : account.CurrentSession().username;
    static const char* kTabNames[] = {"Create", "Play", "Community", "Account", "Settings", "Achievements",
                                      "Friends", "Market", "Servers", "Forums", "Downloads", "Admin",
                                      "What's New", "Tips"};
    const int nPresets = (int)(sizeof(kAccentPresets) / sizeof(kAccentPresets[0]));

    // ---- Page state --------------------------------------------------------
    static unsigned shown = 0;                 // g_pfSerial the state below belongs to
    static okay::ProfileBundle s_b;            // what's on screen
    static bool s_loaded = false;
    static std::vector<PfJob*> s_queue;        // jobs waiting for the worker
    static std::vector<okay::Achievement> s_local;   // my local unlocks (newest first)
    static PfLocalShowcase s_localShow;        // my device-side picks
    static bool s_edit = false;                // "Edit showcase" mode (own profile)
    static std::vector<std::string> e_ach; static std::vector<long> e_games; static int e_banner = -1;
    auto refreshLocal = [&]() {
        s_local = okay::Achievements::All();
        std::reverse(s_local.begin(), s_local.end());
        s_localShow = LoadLocalShowcase();
    };
    auto queueJob = [&](int type) -> PfJob* {
        PfJob* j = new PfJob; j->type = type; j->serial = shown;
        j->target = !s_b.p.id.empty() ? s_b.p.id : (!g_pfReq.uid.empty() ? g_pfReq.uid : g_pfReq.username);
        j->byName = s_b.p.id.empty() && g_pfReq.uid.empty();
        s_queue.push_back(j);
        return j;
    };
    if (shown != g_pfSerial) {                 // a (new) profile was opened
        shown = g_pfSerial;
        s_b = okay::ProfileBundle(); s_loaded = false; s_edit = false;
        s_b.p.id = g_pfReq.uid; s_b.p.username = g_pfReq.username; s_b.p.avatar = g_pfReq.avatar;
        s_b.p.display = !g_pfReq.name.empty() ? g_pfReq.name : g_pfReq.username;
        s_b.p.accent = g_pfReq.accent;
        s_b.isMe = (!meName.empty() && g_pfReq.username == meName) || (!g_pfReq.uid.empty() && g_pfReq.uid == account.UserId());
        if (kDemo) { s_b = DemoProfileBundle(g_pfReq, meName); s_loaded = true; }
        else if (account.IsLoggedIn()) {
            if (s_b.isMe) refreshLocal();
            PfJob* j = queueJob(0); j->syncLocal = s_b.isMe;
        }
    }
    // Feed the worker one job at a time and pick up results (never blocks).
    if (!kDemo) {
        if (!g_pfBusy && !s_queue.empty()) {
            PfJob* j = s_queue.front(); s_queue.erase(s_queue.begin());
            g_pfBusy = true;
            if (okay::thr::Thread* t = okay::thr::CreateThread(ProfileWorker, "okay-profile", j)) okay::thr::DetachThread(t);
            else { g_pfBusy = false; delete j; }
        }
        if (g_pfMutex) {
            okay::thr::LockMutex(g_pfMutex);
            if (g_pfReady) {
                if (g_pfReadySerial == shown && (g_pfResult.found || !s_loaded)) {
                    bool wasMe = s_b.isMe;
                    s_b = g_pfResult; s_loaded = true;
                    if (s_b.isMe && !wasMe) refreshLocal();
                }
                g_pfReady = false;
            }
            int at = g_pfActType, ar = g_pfActResult; g_pfActType = 0; g_pfActResult = 0;
            okay::thr::UnlockMutex(g_pfMutex);
            if (at == 1) Toast(ar > 0 ? "Showcase saved" : "Showcase saved on this device (server showcases aren't enabled yet)", ar > 0 ? 1 : 2);
            else if (at == 2) Toast(ar > 0 ? "Friend request sent" : "Couldn't send the friend request", ar > 0 ? 1 : 2);
            else if (at == 3) Toast(ar > 0 ? "Friend request accepted" : "Couldn't accept the request", ar > 0 ? 1 : 2);
            else if (at == 4 && ar < 0) Toast("Couldn't update the friendship", 2);
            else if (at == 5) Toast(ar > 0 ? "Following" : "Couldn't follow â try again", ar > 0 ? 1 : 2);
            else if (at == 6 && ar < 0) Toast("Couldn't unfollow", 2);
        }
    }

    const okay::PublicProfile& p = s_b.p;
    const bool me = s_b.isMe || (!meName.empty() && p.username == meName);
    const std::string dispName = !p.display.empty() ? p.display : (!p.username.empty() ? p.username : std::string("Player"));
    const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 cDim  = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 cRaised = ImGui::GetColorU32(okaytheme::kRaised);
    const ImU32 hair = IM_COL32(255, 255, 255, 16);
    const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
    const ImU32 cGreen = IM_COL32(76, 200, 110, 255), cGrey = IM_COL32(120, 128, 140, 255);
    const float fs = ImGui::GetFontSize();
    ImFont* font = ImGui::GetFont();

    // Back: Esc or the mouse's back button (Esc leaves edit mode first).
    bool goBack = false;
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(3))) {
        if (s_edit) s_edit = false; else goBack = true;
    }

    // ---- Top bar: "< Back to Friends" pill (+ a loading hint) ----------------
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cp = ImGui::GetCursorScreenPos();
        const float ch = ImGui::GetTextLineHeight() + 14.0f;
        char lbl[64]; std::snprintf(lbl, sizeof(lbl), "Back to %s", kTabNames[std::clamp(fromTab, 0, 13)]);
        float bw = ImGui::CalcTextSize(lbl).x + 42.0f;
        if (ImGui::InvisibleButton("##pfback", ImVec2(bw, ch))) goBack = true;
        bool h = ImGui::IsItemHovered();
        if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddRectFilled(cp, ImVec2(cp.x + bw, cp.y + ch), h ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 9), ch * 0.5f);
        float cy = cp.y + ch * 0.5f;
        dl->AddLine(ImVec2(cp.x + 20.0f, cy - 4.5f), ImVec2(cp.x + 15.5f, cy), cText, 1.8f);
        dl->AddLine(ImVec2(cp.x + 15.5f, cy), ImVec2(cp.x + 20.0f, cy + 4.5f), cText, 1.8f);
        dl->AddText(ImVec2(cp.x + 29.0f, cp.y + (ch - ImGui::GetTextLineHeight()) * 0.5f), h ? cText : cDim, lbl);
        if (!kDemo && (g_pfBusy || !s_queue.empty())) {
            float a0 = (float)ImGui::GetTime() * 6.0f;
            ImVec2 sc(cp.x + bw + 20.0f, cy);
            dl->PathArcTo(sc, 6.0f, a0, a0 + 4.6f, 20); dl->PathStroke(ImGui::GetColorU32(accLt), 0, 2.0f);
            dl->AddText(ImVec2(sc.x + 14.0f, cp.y + (ch - ImGui::GetTextLineHeight()) * 0.5f), cDim,
                        s_loaded ? "Updating\xE2\x80\xA6" : "Loading profile\xE2\x80\xA6");
        }
        ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + ch + 14.0f));
        ImGui::Dummy(ImVec2(0, 0));
    }
    if (goBack) { ClosePublicProfile(); s_edit = false; return out; }

    // Centered empty state (signed out / not found).
    auto emptyState = [&](int icon, const char* title, const char* sub) { EmptyState(icon, title, sub, 50.0f); };
    if (!kDemo && !account.IsLoggedIn()) {
        emptyState(3, "Sign in to view profiles", "Profiles, showcases and friends need an OkaySpace account.");
        return out;
    }
    if (s_loaded && !s_b.found) {
        if (s_b.reached && !s_b.failed) emptyState(3, "Profile not found", "This player may have renamed or deleted their account.");
        else emptyState(3, "Couldn't load this profile", "The server didn't answer properly. Check your connection and try again.");
        if (!s_b.reached || s_b.failed) {
            const float bw = 120.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - bw) * 0.5f));
            if (PrimaryButton("Retry", ImVec2(bw, 34))) { account.ReloadSessionToken(); ++g_pfSerial; }
        }
        return out;
    }

    // ---- Derived data --------------------------------------------------------
    const std::vector<okay::Achievement>& achAll = (me && !kDemo) ? s_local : s_b.achievements;
    const bool achKnown = me || s_b.achSupported;
    const std::vector<std::string> pickA = s_b.showcaseSupported ? p.showcaseAch : (me ? s_localShow.ach : std::vector<std::string>());
    const std::vector<long> pickG = s_b.showcaseSupported ? p.showcaseGames : (me ? s_localShow.games : std::vector<long>());
    const int pickBanner = s_b.showcaseSupported ? p.bannerAccent : (me ? s_localShow.banner : -1);
    auto findAch = [&](const std::string& id) -> const okay::Achievement* {
        for (const auto& a : achAll) if (a.id == id) return &a;
        return nullptr;
    };
    auto findGame = [&](long id) -> const okay::WorkshopItem* {
        for (const auto& g : s_b.games) if (g.id == id) return &g;
        return nullptr;
    };
    std::vector<okay::Achievement> shownA;
    for (const auto& id : pickA) {
        if (shownA.size() >= 6) break;
        if (const okay::Achievement* a = findAch(id)) shownA.push_back(*a);
        else if (!achKnown) { okay::Achievement x; x.id = id; x.label = PfPrettyId(id); shownA.push_back(x); }
    }
    const bool achPinned = !shownA.empty();
    if (!achPinned) for (std::size_t i = 0; i < achAll.size() && i < 6; ++i) shownA.push_back(achAll[i]);
    std::vector<okay::WorkshopItem> shownG;
    for (long id : pickG) { if (shownG.size() >= 4) break; if (const okay::WorkshopItem* g = findGame(id)) shownG.push_back(*g); }
    const bool gamesPinned = !shownG.empty();
    if (!gamesPinned) for (std::size_t i = 0; i < s_b.games.size() && i < 4; ++i) shownG.push_back(s_b.games[i]);
    long totalDl = 0; for (const auto& g : s_b.games) totalDl += g.downloads;
    const int achN = (int)achAll.size(), gameN = (int)s_b.games.size();
    (void)totalDl;
    const int achPts = okay::Achievements::TotalPoints(achAll);
    const int level = okay::Achievements::Level(achPts) + 1;   // points from achievements level you up
    // Time played: theirs from the server; mine from this device until the table exists.
    if (me && !kDemo) LoadPlaytime();
    const std::vector<okay::PlaytimeEntry>& playAll = (me && !kDemo && s_b.playtime.empty()) ? g_playtime : s_b.playtime;
    long playSecs = 0, editorSecs = 0;
    for (const auto& e : playAll) { if (e.key == "editor") editorSecs += e.seconds; else playSecs += e.seconds; }
    int bannerIdx = (s_edit ? e_banner : pickBanner);
    if (bannerIdx < 0 || bannerIdx >= nPresets) bannerIdx = p.accent;
    if (bannerIdx < 0 || bannerIdx >= nPresets) bannerIdx = 0;
    const ImVec4 bc = kAccentPresets[bannerIdx].col;
    const bool playingSomething = p.online && !p.playing.empty() && p.playing != "In OkaySpace" &&
                                  p.playing != "In the launcher" && p.playing != "Away";
    const ImU32 presCol = !p.online ? cGrey : playingSomething ? ImGui::GetColorU32(accLt) : cGreen;

    auto enterEdit = [&]() {
        s_edit = true; e_ach.clear(); e_games.clear();
        for (const auto& a : shownA) if (achPinned) e_ach.push_back(a.id);
        for (const auto& g : shownG) if (gamesPinned) e_games.push_back(g.id);
        e_banner = pickBanner;
    };
    auto saveEdit = [&]() {
        if (kDemo) {
            s_b.p.showcaseAch = e_ach; s_b.p.showcaseGames = e_games; s_b.p.bannerAccent = e_banner;
            Toast("Showcase saved", 1);
        } else {
            PfLocalShowcase ls; ls.ach = e_ach; ls.games = e_games; ls.banner = e_banner;
            SaveLocalShowcase(ls); s_localShow = ls;
            if (s_b.showcaseSupported) { s_b.p.showcaseAch = e_ach; s_b.p.showcaseGames = e_games; s_b.p.bannerAccent = e_banner; }
            PfJob* j = queueJob(1); j->ach = e_ach; j->games = e_games; j->banner = e_banner;
        }
        s_edit = false;
    };

    // Small-caps card title with optional right-side text (clickable if link).
    auto cardTitle = [&](const char* title, const char* right, bool link) -> bool {
        bool hit = false;
        const float x0 = ImGui::GetCursorPosX(), avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetWindowFontScale(0.80f);
        ImGui::TextDisabled("%s", title);
        if (right && *right) {
            float rw = ImGui::CalcTextSize(right).x;
            ImGui::SameLine(std::max(0.0f, x0 + avail - rw));
            if (link) {
                ImGui::PushStyleColor(ImGuiCol_Text, accLt);
                ImGui::TextUnformatted(right);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                hit = ImGui::IsItemClicked();
            } else ImGui::TextDisabled("%s", right);
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Dummy(ImVec2(0, 4));
        return hit;
    };
    auto beginCard = [&](const char* id, float w) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
        ImGui::BeginChild(id, ImVec2(w, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
    };
    auto endCard = [&]() {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8));
    };
    // Pick badge (edit mode): numbered accent disc, or a faint "+" when unpicked.
    auto pickBadge = [&](ImDrawList* dl, ImVec2 c, int n, bool hov) {
        if (n > 0) {
            dl->AddCircleFilled(c, 10.0f, ImGui::GetColorU32(kAccent), 20);
            char t[4]; std::snprintf(t, sizeof(t), "%d", n);
            ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32_WHITE, t);
        } else {
            dl->AddCircle(c, 9.0f, hov ? cText : IM_COL32(255, 255, 255, 60), 20, 1.5f);
            ImU32 pc = hov ? cText : IM_COL32(255, 255, 255, 90);
            dl->AddLine(ImVec2(c.x - 4, c.y), ImVec2(c.x + 4, c.y), pc, 1.5f);
            dl->AddLine(ImVec2(c.x, c.y - 4), ImVec2(c.x, c.y + 4), pc, 1.5f);
        }
    };

    ImGui::BeginChild("##pfbody", ImVec2(0, ImGui::GetContentRegionAvail().y - 46.0f), false);
    const float W = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ======================= HERO: banner, avatar, name, actions, stats =======
    {
        const bool compact = W < 640.0f;
        const float bannerH = W < 520.0f ? 104.0f : 132.0f;
        const float A = W < 520.0f ? 84.0f : 104.0f;
        const float infoH = compact ? (A * 0.5f + 14.0f + 70.0f + 36.0f + 14.0f) : 98.0f;
        const int statCols = W < 460.0f ? 2 : 4;
        const float statRowH = 62.0f, statsH = statRowH * (4 / statCols) + 6.0f;
        const float heroH = bannerH + infoH + statsH;
        ImVec2 hp = ImGui::GetCursorScreenPos();
        ImVec2 he(hp.x + W, hp.y + heroH);
        dl->AddRectFilled(hp, he, cRaised, 12.0f);
        // Banner: profile-accent gradient + soft shapes, fading into the card.
        int v0 = dl->VtxBuffer.Size;
        dl->AddRectFilled(hp, ImVec2(he.x, hp.y + bannerH), IM_COL32_WHITE, 12.0f, ImDrawFlags_RoundCornersTop);
        PfGradient(dl, v0, hp, ImVec2(he.x, hp.y + bannerH),
                   okaytheme::Mix(okaytheme::kRaised, bc, 0.78f), okaytheme::Mix(okaytheme::kRaised, bc, 0.18f));
        dl->PushClipRect(hp, ImVec2(he.x, hp.y + bannerH), true);
        dl->AddCircleFilled(ImVec2(he.x - W * 0.16f, hp.y - bannerH * 0.15f), bannerH * 1.05f, IM_COL32(255, 255, 255, 10), 64);
        dl->AddCircleFilled(ImVec2(he.x - W * 0.05f, hp.y + bannerH * 1.05f), bannerH * 0.65f, IM_COL32(255, 255, 255, 8), 48);
        dl->AddCircleFilled(ImVec2(hp.x + W * 0.42f, hp.y + bannerH * 1.3f), bannerH * 0.8f, IM_COL32(0, 0, 0, 22), 48);
        for (int k = 0; k < 7; ++k) {
            float x = hp.x + W * 0.55f + k * 26.0f;
            dl->AddLine(ImVec2(x, hp.y + bannerH + 10), ImVec2(x + bannerH * 0.9f, hp.y - 10), IM_COL32(255, 255, 255, 6), 9.0f);
        }
        dl->PopClipRect();
        v0 = dl->VtxBuffer.Size;
        dl->AddRectFilled(ImVec2(hp.x, hp.y + bannerH - 46.0f), ImVec2(he.x, hp.y + bannerH), IM_COL32_WHITE);
        PfGradient(dl, v0, ImVec2(hp.x, hp.y + bannerH - 46.0f), ImVec2(hp.x, hp.y + bannerH),
                   okaytheme::WithAlpha(okaytheme::kRaised, 0.0f), okaytheme::WithAlpha(okaytheme::kRaised, 0.85f));
        // Level badge (top-right of the banner).
        {
            ImVec2 lc(he.x - 42.0f, hp.y + 38.0f);
            dl->AddCircleFilled(lc, 24.0f, IM_COL32(0, 0, 0, 80), 40);
            dl->AddCircle(lc, 21.0f, ImGui::GetColorU32(okaytheme::Mix(bc, ImVec4(1, 1, 1, 1), 0.35f)), 40, 2.5f);
            char lv[8]; std::snprintf(lv, sizeof(lv), "%d", level);
            ImVec2 ls = font->CalcTextSizeA(fs * 1.2f, FLT_MAX, 0.0f, lv);
            dl->AddText(font, fs * 1.2f, ImVec2(lc.x - ls.x * 0.5f, lc.y - ls.y * 0.5f), IM_COL32_WHITE, lv);
            ImVec2 ks = font->CalcTextSizeA(fs * 0.72f, FLT_MAX, 0.0f, "LEVEL");
            dl->AddText(font, fs * 0.72f, ImVec2(lc.x - ks.x * 0.5f, lc.y + 27.0f), IM_COL32(255, 255, 255, 190), "LEVEL");
            if (ImGui::IsMouseHoveringRect(ImVec2(lc.x - 26, lc.y - 26), ImVec2(lc.x + 26, lc.y + 40)) && ImGui::IsWindowHovered())
                ImGui::SetTooltip("Level %d  \xC2\xB7  %d achievement points\n%d%% to level %d. Every achievement earns points.", level, achPts,
                                  (int)(okay::Achievements::LevelProgress(achPts) * 100), level + 1);
        }
        // Avatar with a presence ring (cut out of the card).
        ImVec2 ac(hp.x + 26.0f + A * 0.5f, hp.y + bannerH - 6.0f);
        dl->AddCircleFilled(ac, A * 0.5f + 7.0f, cRaised, 64);
        dl->AddCircle(ac, A * 0.5f + 3.0f, presCol, 64, 3.0f);
        if (SDL_Texture* tex = AvatarTexture(p.avatar)) {
            dl->AddImageRounded((ImTextureID)(intptr_t)tex, ImVec2(ac.x - A * 0.5f, ac.y - A * 0.5f), ImVec2(ac.x + A * 0.5f, ac.y + A * 0.5f),
                                ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, A * 0.5f);
        } else {
            ImVec4 nc = NameColor(dispName);
            dl->AddCircleFilled(ac, A * 0.5f, ImGui::GetColorU32(ImVec4(nc.x * 0.55f, nc.y * 0.55f, nc.z * 0.55f, 1)), 64);
            char init[2] = {(char)std::toupper((unsigned char)dispName[0]), 0};
            ImVec2 is = font->CalcTextSizeA(A * 0.44f, FLT_MAX, 0.0f, init);
            dl->AddText(font, A * 0.44f, ImVec2(ac.x - is.x * 0.5f, ac.y - is.y * 0.5f), IM_COL32_WHITE, init);
        }
        ImVec2 pd(ac.x + A * 0.354f, ac.y + A * 0.354f);
        dl->AddCircleFilled(pd, 11.0f, cRaised, 20);
        dl->AddCircleFilled(pd, 7.5f, presCol, 20);

        // Actions: right of the name on wide cards, under it on compact ones.
        const float bh = ImGui::GetTextLineHeight() + 16.0f, rb = bh;   // button height, round "more" size
        struct Btn { std::string label; int kind; };   // kind: 0 secondary, 1 primary
        std::vector<Btn> btns;
        enum { kBMsg, kBFriend, kBEdit, kBProfile, kBCancel, kBSave, kBFollow };
        std::vector<int> ids;
        if (me) {
            if (s_edit) { btns.push_back({"Cancel", 0}); ids.push_back(kBCancel); btns.push_back({"Save showcase", 1}); ids.push_back(kBSave); }
            else { btns.push_back({"Edit profile", 0}); ids.push_back(kBProfile); btns.push_back({"Edit showcase", 1}); ids.push_back(kBEdit); }
        } else {
            const bool friends = s_b.friendStatus == "accepted";
            btns.push_back({"Message", friends ? 1 : 0}); ids.push_back(kBMsg);
            std::string fl = friends ? "Friends" : s_b.friendStatus == "pending" ? (s_b.incoming ? "Accept request" : "Request sent") : "Add friend";
            btns.push_back({fl, (!friends && !(s_b.friendStatus == "pending" && !s_b.incoming)) ? 1 : 0}); ids.push_back(kBFriend);
            if (s_b.followSupported) {
                btns.push_back({s_b.iFollow ? "Following" : s_b.followsMe ? "Follow back" : "Follow", 0});
                ids.push_back(kBFollow);
            }
        }
        float actW = rb;
        for (const auto& b : btns) actW += ImGui::CalcTextSize(b.label.c_str()).x + 30.0f + 8.0f;
        const float nameX = compact ? hp.x + 26.0f : hp.x + 26.0f + A + 22.0f;
        const float nameY = compact ? hp.y + bannerH + A * 0.5f + 12.0f : hp.y + bannerH + 12.0f;
        const float textMaxW = compact ? W - 52.0f : std::max(80.0f, (he.x - 22.0f - actW) - nameX - 16.0f);
        float bx = compact ? nameX : he.x - 22.0f - actW;
        const float by = compact ? nameY + 74.0f : hp.y + bannerH + 16.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(15, 8));
        for (std::size_t i = 0; i < btns.size(); ++i) {
            const Btn& b = btns[i];
            float w = ImGui::CalcTextSize(b.label.c_str()).x + 30.0f;
            ImGui::SetCursorScreenPos(ImVec2(bx, by));
            ImGui::PushID((int)i);
            bool disabled = !kDemo && ((ids[i] == kBMsg || ids[i] == kBFriend || ids[i] == kBFollow) && p.id.empty());
            ImGui::BeginDisabled(disabled);
            bool hit;
            if (b.kind == 1) hit = PrimaryButton(b.label.c_str(), ImVec2(w, bh));
            else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.07f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.14f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.20f));
                hit = ImGui::Button(b.label.c_str(), ImVec2(w, bh));
                ImGui::PopStyleColor(3);
            }
            ImGui::EndDisabled();
            if (ids[i] == kBFollow && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", s_b.iFollow ? "You follow them - click to unfollow"
                                                    : "See their activity in your feed. No request needed.");
            if (ids[i] == kBFriend && ImGui::IsItemHovered() && !s_b.friendStatus.empty() && !s_b.incoming)
                ImGui::SetTooltip(s_b.friendStatus == "accepted" ? "You're friends - click for options" : "Waiting for them to accept");
            ImGui::PopID();
            if (hit) {
                switch (ids[i]) {
                case kBMsg: OpenChatWith(p.id, dispName, p.avatar); break;
                case kBEdit: enterEdit(); break;
                case kBCancel: s_edit = false; break;
                case kBSave: saveEdit(); break;
                case kBProfile: out.gotoTab = 3; ClosePublicProfile(); break;
                case kBFollow:
                    if (s_b.iFollow) ImGui::OpenPopup("##pffollow");
                    else {
                        if (kDemo) Toast("Following " + dispName, 1);
                        else queueJob(5);
                        s_b.iFollow = true; if (s_b.followers >= 0) ++s_b.followers;
                    }
                    break;
                case kBFriend:
                    if (s_b.friendStatus == "accepted" || (s_b.friendStatus == "pending" && !s_b.incoming))
                        ImGui::OpenPopup("##pffriend");
                    else if (s_b.friendStatus == "pending" && s_b.incoming) {
                        if (kDemo) Toast("You're now friends with " + dispName, 1);
                        else { PfJob* j = queueJob(3); j->row = s_b.friendRow; }
                        s_b.friendStatus = "accepted";
                    } else {
                        if (kDemo) Toast("Friend request sent", 1);
                        else queueJob(2);
                        s_b.friendStatus = "pending"; s_b.incoming = false;
                    }
                    break;
                }
            }
            bx += w + 8.0f;
        }
        ImGui::PopStyleVar(2);
        ImGui::SetCursorScreenPos(ImVec2(bx, by + (bh - rb) * 0.5f));
        if (RoundIconButton("##pfmore", kRibMore, rb, IM_COL32(255, 255, 255, 18), IM_COL32(255, 255, 255, 36), cText, "More"))
            ImGui::OpenPopup("##pfmorepop");
        if (ImGui::BeginPopup("##pffollow")) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1));
            if (ImGui::MenuItem("Unfollow")) {
                if (!kDemo) queueJob(6); else Toast("Unfollowed", 1);
                s_b.iFollow = false; if (s_b.followers > 0) --s_b.followers;
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("##pffriend")) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1));
            if (ImGui::MenuItem(s_b.friendStatus == "accepted" ? "Remove friend" : "Cancel request")) {
                if (!kDemo) { PfJob* j = queueJob(4); j->row = s_b.friendRow; }
                else Toast(s_b.friendStatus == "accepted" ? "Removed from friends" : "Request cancelled", 1);
                s_b.friendStatus.clear(); s_b.friendRow = 0;
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("##pfmorepop")) {
            if (ImGui::MenuItem("Copy username")) { ImGui::SetClipboardText(("@" + p.username).c_str()); Toast("Username copied", 1); }
            if (!me && p.online && !p.sessionCode.empty() && ImGui::MenuItem("Join their game")) MpJoinFriend(p.sessionCode, p.username);
            if (!me && MpCanInvite() && ImGui::MenuItem(("Invite to " + MpGameTitle()).c_str())) MpInviteUser(p.id, p.username);
            if (!me && s_b.friendStatus == "accepted" && ImGui::MenuItem(MpPartyId().empty() ? "Start a party with them" : "Invite to party")) MpPartyInvite(p.id, p.username);
            if (me && ImGui::MenuItem("View all achievements")) { out.gotoTab = 5; ClosePublicProfile(); }
            if (!me && ImGui::MenuItem("Report user\xE2\x80\xA6")) OpenReport(okayrep::Target::User, p.id, p.username);
            if (!me && s_b.friendStatus == "pending" && s_b.incoming) {
                ImGui::Separator();
                if (ImGui::MenuItem("Decline request")) {
                    if (!kDemo) { PfJob* j = queueJob(4); j->row = s_b.friendRow; }
                    s_b.friendStatus.clear(); s_b.friendRow = 0;
                }
            }
            ImGui::EndPopup();
        }

        // Name, @handle + presence, custom status.
        {
            float nsz = fs * 1.5f;
            std::string nm = dispName;
            while (nm.size() > 4 && font->CalcTextSizeA(nsz, FLT_MAX, 0.0f, nm.c_str()).x > textMaxW - (me ? 46.0f : 0.0f))
                nm = nm.substr(0, nm.size() - 4) + "\xE2\x80\xA6";
            dl->AddText(font, nsz, ImVec2(nameX, nameY), IM_COL32(242, 244, 250, 255), nm.c_str());
            float afterName = nameX + font->CalcTextSizeA(nsz, FLT_MAX, 0.0f, nm.c_str()).x;
            if (p.verified) {   // blue check
                const ImVec2 vc(afterName + 14.0f, nameY + nsz * 0.55f);
                DrawVerifiedBadge(dl, vc, nsz * 0.36f);
                if (ImGui::IsMouseHoveringRect(ImVec2(vc.x - 10, vc.y - 10), ImVec2(vc.x + 10, vc.y + 10))) ImGui::SetTooltip("Verified");
                afterName += 26.0f;
            }
            if (me) {   // "YOU" pill after the name
                float nx = afterName + 10.0f;
                ImVec2 ts = font->CalcTextSizeA(fs * 0.72f, FLT_MAX, 0.0f, "YOU");
                float py = nameY + (nsz - ts.y - 6.0f) * 0.5f + 1.0f;
                dl->AddRectFilled(ImVec2(nx, py), ImVec2(nx + ts.x + 12.0f, py + ts.y + 6.0f), ImGui::GetColorU32(okaytheme::WithAlpha(kAccent, 0.28f)), 8.0f);
                dl->AddText(font, fs * 0.72f, ImVec2(nx + 6.0f, py + 3.0f), ImGui::GetColorU32(accLt), "YOU");
            } else if (s_b.followsMe) {   // "FOLLOWS YOU" pill
                float nx = afterName + 10.0f;
                const char* fy = "FOLLOWS YOU";
                ImVec2 ts = font->CalcTextSizeA(fs * 0.72f, FLT_MAX, 0.0f, fy);
                float py = nameY + (nsz - ts.y - 6.0f) * 0.5f + 1.0f;
                if (nx + ts.x + 12.0f < nameX + textMaxW + 40.0f) {
                    dl->AddRectFilled(ImVec2(nx, py), ImVec2(nx + ts.x + 12.0f, py + ts.y + 6.0f), IM_COL32(255, 255, 255, 22), 8.0f);
                    dl->AddText(font, fs * 0.72f, ImVec2(nx + 6.0f, py + 3.0f), cDim, fy);
                }
            }
            float y2 = nameY + nsz + 6.0f;
            std::string handle = p.username.empty() ? std::string() : "@" + p.username;
            dl->AddText(ImVec2(nameX, y2), cDim, handle.c_str());
            float hx = nameX + ImGui::CalcTextSize(handle.c_str()).x + (handle.empty() ? 0.0f : 12.0f);
            std::string pres = !s_loaded && !kDemo ? std::string()
                             : playingSomething ? p.playing
                             : p.online ? std::string("Online")
                             : p.lastSeen > 0 ? "Offline  \xC2\xB7  seen " + RelTime(p.lastSeen) : std::string("Offline");
            if (!pres.empty()) {
                float ly = y2 + ImGui::GetTextLineHeight() * 0.5f;
                dl->AddCircleFilled(ImVec2(hx + 4.0f, ly), 4.0f, presCol, 12);
                dl->AddText(ImVec2(hx + 13.0f, y2), playingSomething ? ImGui::GetColorU32(accLt) : p.online ? cGreen : cDim,
                            FitText(pres, nameX + textMaxW - hx - 13.0f).c_str());
            }
            if (!p.customStatus.empty()) {
                std::string cs = "\"" + p.customStatus + "\"";
                dl->AddText(ImVec2(nameX, y2 + ImGui::GetTextLineHeight() + 6.0f), IM_COL32(184, 191, 209, 255),
                            FitText(cs, textMaxW).c_str());
            }
        }

        // Stats strip.
        {
            const float sy = hp.y + bannerH + infoH;
            dl->AddLine(ImVec2(hp.x + 18.0f, sy), ImVec2(he.x - 18.0f, sy), hair, 1.0f);
            char v0s[24], v1s[24], v2s[24], v3s[24];
            if (s_b.friends >= 0) std::snprintf(v0s, sizeof(v0s), "%d", s_b.friends); else std::snprintf(v0s, sizeof(v0s), "\xE2\x80\x94");
            if (s_b.followSupported) {   // followers replace "games published" (they have their own card)
                if (s_b.followers >= 0) std::snprintf(v1s, sizeof(v1s), "%d", s_b.followers); else std::snprintf(v1s, sizeof(v1s), "\xE2\x80\x94");
            } else std::snprintf(v1s, sizeof(v1s), "%d", gameN);
            if (achKnown) std::snprintf(v2s, sizeof(v2s), "%d", achPts); else std::snprintf(v2s, sizeof(v2s), "\xE2\x80\x94");
            if (playSecs + editorSecs > 0) std::snprintf(v3s, sizeof(v3s), "%ld", (playSecs + editorSecs) / 3600);
            else std::snprintf(v3s, sizeof(v3s), "\xE2\x80\x94");
            const char* vals[4] = {v0s, v1s, v2s, v3s};
            const char* labs[4] = {"FRIENDS", s_b.followSupported ? "FOLLOWERS" : "GAMES PUBLISHED", "ACHIEVEMENT POINTS", "HOURS PLAYED"};
            const float tw = W / statCols;
            for (int i = 0; i < 4; ++i) {
                float tx = hp.x + (i % statCols) * tw, ty = sy + 3.0f + (i / statCols) * statRowH;
                if (i % statCols) dl->AddLine(ImVec2(tx, ty + 14.0f), ImVec2(tx, ty + statRowH - 14.0f), hair, 1.0f);
                dl->AddText(font, fs * 1.3f, ImVec2(tx + 24.0f, ty + 11.0f), IM_COL32(242, 244, 250, 255), vals[i]);
                dl->AddText(font, fs * 0.74f, ImVec2(tx + 24.0f, ty + 13.0f + fs * 1.3f + 2.0f), cDim, labs[i]);
                if (i == 1 && s_b.followSupported && s_b.following >= 0 &&
                    ImGui::IsMouseHoveringRect(ImVec2(tx, ty), ImVec2(tx + tw, ty + statRowH)))
                    ImGui::SetTooltip("%s follow%s %d player%s", me ? "You" : dispName.c_str(), me ? "" : "s",
                                      s_b.following, s_b.following == 1 ? "" : "s");
            }
        }
        ImGui::SetCursorScreenPos(hp);
        ImGui::Dummy(ImVec2(W, heroH));
        ImGui::Dummy(ImVec2(0, 8));
    }

    // ======================= Cards ============================================
    const bool twoCol = W >= 820.0f;
    const float gap = 16.0f;
    const float sideW = twoCol ? std::clamp(W * 0.31f, 280.0f, 360.0f) : 0.0f;
    const float mainW = twoCol ? W - sideW - gap : W;

    auto editCard = [&](float w) {
        beginCard("##pfeditcard", w);
        cardTitle("EDIT SHOWCASE", nullptr, false);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Click achievements and games below to pin them (up to 6 and 4). They show in the order you pick them.");
        ImGui::PopTextWrapPos();
        if (!kDemo && !s_b.showcaseSupported && s_loaded)
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.36f, 1), "This server hasn't enabled showcases yet, so picks stay on this device.");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextUnformatted("Banner color");
        ImGui::Dummy(ImVec2(0, 2));
        ImVec2 sp = ImGui::GetCursorScreenPos();
        ImDrawList* d = ImGui::GetWindowDrawList();
        const float D = 26.0f;
        for (int k = -1; k < nPresets; ++k) {
            ImVec2 c(sp.x + D * 0.5f + (k + 1) * (D + 10.0f), sp.y + D * 0.5f);
            ImGui::SetCursorScreenPos(ImVec2(c.x - D * 0.5f, sp.y));
            ImGui::PushID(k + 10);
            if (ImGui::InvisibleButton("##sw", ImVec2(D, D))) e_banner = k;
            bool h = ImGui::IsItemHovered();
            ImGui::PopID();
            int pi = k < 0 ? std::clamp(p.accent, 0, nPresets - 1) : k;
            ImVec4 col = kAccentPresets[pi].col;
            if (k < 0) {   // "Auto": follow my profile accent (half-filled disc)
                d->AddCircle(c, D * 0.5f - 1.0f, ImGui::GetColorU32(col), 24, 2.0f);
                d->PathArcTo(c, D * 0.5f - 4.0f, 3.14159f * 0.5f, 3.14159f * 1.5f, 16); d->PathFillConvex(ImGui::GetColorU32(col));
            } else d->AddCircleFilled(c, D * 0.5f - 1.0f, ImGui::GetColorU32(col), 24);
            if (e_banner == k) d->AddCircle(c, D * 0.5f + 3.0f, IM_COL32_WHITE, 24, 2.0f);
            else if (h) d->AddCircle(c, D * 0.5f + 3.0f, IM_COL32(255, 255, 255, 90), 24, 1.5f);
            if (h) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", k < 0 ? "Auto (profile color)" : kAccentPresets[k].name); }
        }
        ImGui::SetCursorScreenPos(ImVec2(sp.x, sp.y + D + 6.0f));
        ImGui::Dummy(ImVec2(0, 0));
        endCard();
    };

    auto achCard = [&](float w) {
        if (!me && !s_edit && shownA.empty() && (s_loaded || kDemo)) return;   // nothing public to show
        beginCard("##pfach", w);
        char right[48];
        if (s_edit) std::snprintf(right, sizeof(right), "%d / 6 pinned", (int)e_ach.size());
        else if (me) std::snprintf(right, sizeof(right), "%d unlocked  \xC2\xBB", achN);
        else if (achKnown) std::snprintf(right, sizeof(right), "%d unlocked", achN);
        else right[0] = '\0';
        if (cardTitle(s_edit ? "PIN ACHIEVEMENTS" : achPinned ? "ACHIEVEMENT SHOWCASE" : "RECENT ACHIEVEMENTS", right, me && !s_edit)) {
            out.gotoTab = 5; ClosePublicProfile();
        }
        const std::vector<okay::Achievement>& list = s_edit ? achAll : shownA;
        if (list.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            if (!s_loaded && !kDemo && !me) ImGui::TextDisabled("Loading\xE2\x80\xA6");
            else if (me) ImGui::TextDisabled("No achievements yet. Play games that unlock achievements, then pin your favourites here.");
            ImGui::PopTextWrapPos();
        } else {
            const float iw = ImGui::GetContentRegionAvail().x, g = 10.0f, th = 66.0f;
            const int cols = iw >= 560.0f ? 3 : iw >= 300.0f ? 2 : 1;
            const float tw = (iw - g * (cols - 1)) / cols;
            ImVec2 o = ImGui::GetCursorScreenPos();
            ImDrawList* d = ImGui::GetWindowDrawList();
            for (std::size_t i = 0; i < list.size(); ++i) {
                const okay::Achievement& a = list[i];
                ImVec2 tp(o.x + (i % cols) * (tw + g), o.y + (i / cols) * (th + g));
                ImGui::SetCursorScreenPos(tp);
                ImGui::PushID((int)i);
                bool click = ImGui::InvisibleButton("##achtile", ImVec2(tw, th));
                bool h = ImGui::IsItemHovered();
                ImGui::PopID();
                int pick = 0;
                if (s_edit) for (std::size_t k = 0; k < e_ach.size(); ++k) if (e_ach[k] == a.id) pick = (int)k + 1;
                const bool faded = s_edit && pick == 0;
                d->AddRectFilled(tp, ImVec2(tp.x + tw, tp.y + th), h ? IM_COL32(255, 255, 255, 18) : IM_COL32(255, 255, 255, 8), 10.0f);
                if (pick) d->AddRect(tp, ImVec2(tp.x + tw, tp.y + th), ImGui::GetColorU32(kAccent), 10.0f, 0, 2.0f);
                DrawAchMedal(d, ImVec2(tp.x + 12.0f + 21.0f, tp.y + th * 0.5f), 21.0f, a.id, faded ? 0.45f : 1.0f);
                const float tx = tp.x + 66.0f, maxW = tw - 66.0f - (s_edit ? 28.0f : 10.0f);
                d->AddText(ImVec2(tx, tp.y + 14.0f), faded ? ImGui::GetColorU32(ImGuiCol_Text, 0.55f) : cText, FitText(a.label, maxW).c_str());
                char when[48] = "";
                if (a.when > 0) { std::time_t t = (std::time_t)a.when; if (std::tm* tm = std::localtime(&t)) std::strftime(when, sizeof(when), "Unlocked %b %d, %Y", tm); }
                d->AddText(ImVec2(tx, tp.y + 36.0f), cDim, FitText(when[0] ? when : "Unlocked", maxW).c_str());
                if (s_edit) pickBadge(d, ImVec2(tp.x + tw - 16.0f, tp.y + 16.0f), pick, h);
                if (h) {
                    if (s_edit) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    else ImGui::SetTooltip("%s%s%s", a.label.c_str(), when[0] ? "\n" : "", when);
                }
                if (click && s_edit) {
                    if (pick) e_ach.erase(e_ach.begin() + (pick - 1));
                    else if (e_ach.size() < 6) e_ach.push_back(a.id);
                    else Toast("You can pin up to 6 achievements", 2);
                }
            }
            int rows = ((int)list.size() + cols - 1) / cols;
            ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rows * th + (rows - 1) * g));
            ImGui::Dummy(ImVec2(0, 0));
        }
        endCard();
    };

    auto gamesCard = [&](float w) {
        if (!me && !s_edit && s_b.games.empty() && (s_loaded || kDemo)) return;
        beginCard("##pfgames", w);
        char right[48];
        if (s_edit) std::snprintf(right, sizeof(right), "%d / 4 pinned", (int)e_games.size());
        else std::snprintf(right, sizeof(right), "%d published", gameN);
        cardTitle(s_edit ? "PIN GAMES" : gamesPinned ? "FEATURED GAMES" : "PUBLISHED GAMES", right, false);
        const std::vector<okay::WorkshopItem>& list = s_edit ? s_b.games : shownG;
        if (list.empty()) {
            if (!s_loaded && !kDemo) ImGui::TextDisabled("Loading\xE2\x80\xA6");
            else {
                ImGui::TextDisabled("You haven't published anything to the Marketplace yet.");
                ImGui::Dummy(ImVec2(0, 2));
                if (PrimaryButton("Publish a game", ImVec2(160, 0))) { out.gotoTab = 7; g_openPublishForm = true; ClosePublicProfile(); }
            }
        } else {
            const float iw = ImGui::GetContentRegionAvail().x, g = 12.0f, coverH = 92.0f, th = coverH + 56.0f;
            const int cols = iw >= 760.0f ? 4 : iw >= 330.0f ? 2 : 1;
            const float tw = (iw - g * (cols - 1)) / cols;
            ImVec2 o = ImGui::GetCursorScreenPos();
            ImDrawList* d = ImGui::GetWindowDrawList();
            for (std::size_t i = 0; i < list.size(); ++i) {
                const okay::WorkshopItem& gm = list[i];
                ImVec2 tp(o.x + (i % cols) * (tw + g), o.y + (i / cols) * (th + g));
                ImGui::SetCursorScreenPos(tp);
                ImGui::PushID((int)gm.id);
                bool click = ImGui::InvisibleButton("##gametile", ImVec2(tw, th));
                bool h = ImGui::IsItemHovered();
                ImGui::PopID();
                int pick = 0;
                if (s_edit) for (std::size_t k = 0; k < e_games.size(); ++k) if (e_games[k] == gm.id) pick = (int)k + 1;
                const bool faded = s_edit && pick == 0;
                const float fa = faded ? 0.45f : 1.0f;
                d->AddRectFilled(tp, ImVec2(tp.x + tw, tp.y + th), h ? IM_COL32(255, 255, 255, 18) : IM_COL32(255, 255, 255, 8), 10.0f);
                // Cover: a gradient in the game's own hue + a play disc + kind pill.
                ImVec4 gc = NameColor(gm.title);
                int v0 = d->VtxBuffer.Size;
                d->AddRectFilled(tp, ImVec2(tp.x + tw, tp.y + coverH), IM_COL32_WHITE, 10.0f, ImDrawFlags_RoundCornersTop);
                PfGradient(d, v0, tp, ImVec2(tp.x + tw, tp.y + coverH),
                           ImVec4(gc.x * 0.78f, gc.y * 0.78f, gc.z * 0.78f, fa), ImVec4(gc.x * 0.26f, gc.y * 0.26f, gc.z * 0.30f, fa));
                d->PushClipRect(tp, ImVec2(tp.x + tw, tp.y + coverH), true);
                d->AddCircleFilled(ImVec2(tp.x + tw * 0.85f, tp.y + coverH * 0.1f), coverH * 0.75f, IM_COL32(255, 255, 255, (int)(14 * fa)), 40);
                std::string ini = gm.title.substr(0, 1);
                float isz = coverH * 0.9f;
                d->AddText(font, isz, ImVec2(tp.x + 12.0f, tp.y + coverH - isz * 0.86f), IM_COL32(255, 255, 255, (int)(34 * fa)), ini.c_str());
                d->PopClipRect();
                ImVec2 pc(tp.x + tw * 0.5f, tp.y + coverH * 0.5f);
                d->AddCircleFilled(pc, h && !s_edit ? 20.0f : 18.0f, IM_COL32(0, 0, 0, (int)(90 * fa)), 32);
                d->AddTriangleFilled(ImVec2(pc.x - 4.5f, pc.y - 7.0f), ImVec2(pc.x - 4.5f, pc.y + 7.0f), ImVec2(pc.x + 7.5f, pc.y),
                                     IM_COL32(255, 255, 255, (int)(235 * fa)));
                std::string kind = gm.kind.empty() ? std::string("GAME") : gm.kind;
                for (auto& c : kind) c = (char)std::toupper((unsigned char)c);
                ImVec2 ks = font->CalcTextSizeA(fs * 0.70f, FLT_MAX, 0.0f, kind.c_str());
                d->AddRectFilled(ImVec2(tp.x + 8, tp.y + 8), ImVec2(tp.x + 8 + ks.x + 12, tp.y + 8 + ks.y + 6), IM_COL32(0, 0, 0, (int)(110 * fa)), 7.0f);
                d->AddText(font, fs * 0.70f, ImVec2(tp.x + 14, tp.y + 11), IM_COL32(255, 255, 255, (int)(220 * fa)), kind.c_str());
                if (pick) d->AddRect(tp, ImVec2(tp.x + tw, tp.y + th), ImGui::GetColorU32(kAccent), 10.0f, 0, 2.0f);
                d->AddText(ImVec2(tp.x + 12.0f, tp.y + coverH + 9.0f), faded ? ImGui::GetColorU32(ImGuiCol_Text, 0.55f) : cText,
                           FitText(gm.title, tw - 24.0f).c_str());
                std::string sub = PfCount(gm.downloads) + (gm.downloads == 1 ? " download" : " downloads");
                d->AddText(ImVec2(tp.x + 12.0f, tp.y + coverH + 29.0f), cDim, FitText(sub, tw - 24.0f).c_str());
                if (s_edit) pickBadge(d, ImVec2(tp.x + tw - 16.0f, tp.y + 16.0f), pick, h);
                if (h) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (!s_edit) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(gm.title.c_str());
                        if (!gm.description.empty()) {
                            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
                            ImGui::TextDisabled("%s", gm.description.c_str());
                            ImGui::PopTextWrapPos();
                        }
                        ImGui::TextColored(accLt, "Click to find it in the Marketplace");
                        ImGui::EndTooltip();
                    }
                }
                if (click) {
                    if (s_edit) {
                        if (pick) e_games.erase(e_games.begin() + (pick - 1));
                        else if (e_games.size() < 4) e_games.push_back(gm.id);
                        else Toast("You can pin up to 4 games", 2);
                    } else { out.gotoTab = 7; out.marketSearch = gm.title; ClosePublicProfile(); }
                }
            }
            int rows = ((int)list.size() + cols - 1) / cols;
            ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rows * th + (rows - 1) * g));
            ImGui::Dummy(ImVec2(0, 0));
        }
        endCard();
    };

    auto aboutCard = [&](float w) {
        beginCard("##pfabout", w);
        cardTitle("ABOUT", nullptr, false);
        ImGui::PushTextWrapPos(0.0f);
        if (!p.bio.empty()) ImGui::TextUnformatted(p.bio.c_str());
        else if (!s_loaded && !kDemo) ImGui::TextDisabled("Loading\xE2\x80\xA6");
        else ImGui::TextDisabled(me ? "No bio yet. Add one under Account \xC2\xBB Customize profile." : "No bio yet.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 6));
        ImDrawList* d = ImGui::GetWindowDrawList();
        auto row = [&](int icon, const char* label, const std::string& value) {
            if (value.empty()) return;
            ImVec2 rp = ImGui::GetCursorScreenPos();
            const float lh = ImGui::GetTextLineHeight();
            ImVec2 c(rp.x + 8.0f, rp.y + lh * 0.5f + 3.0f);
            const ImU32 ic = cDim; const float t = 1.5f;
            if (icon == 0) {        // location pin
                d->AddCircle(ImVec2(c.x, c.y - 2.0f), 4.2f, ic, 16, t);
                d->AddLine(ImVec2(c.x - 3.4f, c.y + 0.4f), ImVec2(c.x, c.y + 6.0f), ic, t);
                d->AddLine(ImVec2(c.x + 3.4f, c.y + 0.4f), ImVec2(c.x, c.y + 6.0f), ic, t);
            } else if (icon == 1) { // favourite game: play in a circle
                d->AddCircle(c, 6.5f, ic, 20, t);
                d->AddTriangleFilled(ImVec2(c.x - 2.0f, c.y - 3.2f), ImVec2(c.x - 2.0f, c.y + 3.2f), ImVec2(c.x + 3.4f, c.y), ic);
            } else if (icon == 2) { // calendar
                d->AddRect(ImVec2(c.x - 6.0f, c.y - 5.0f), ImVec2(c.x + 6.0f, c.y + 6.0f), ic, 2.0f, 0, t);
                d->AddLine(ImVec2(c.x - 6.0f, c.y - 1.5f), ImVec2(c.x + 6.0f, c.y - 1.5f), ic, t);
                d->AddLine(ImVec2(c.x - 2.8f, c.y - 7.0f), ImVec2(c.x - 2.8f, c.y - 3.5f), ic, t);
                d->AddLine(ImVec2(c.x + 2.8f, c.y - 7.0f), ImVec2(c.x + 2.8f, c.y - 3.5f), ic, t);
            } else {                // clock
                d->AddCircle(c, 6.5f, ic, 20, t);
                d->AddLine(c, ImVec2(c.x, c.y - 4.0f), ic, t);
                d->AddLine(c, ImVec2(c.x + 3.0f, c.y + 1.5f), ic, t);
            }
            ImGui::SetCursorScreenPos(ImVec2(rp.x + 24.0f, rp.y + 3.0f));
            ImGui::BeginGroup();
            ImGui::SetWindowFontScale(0.80f);
            ImGui::TextDisabled("%s", label);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(value.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(0, 4));
        };
        char since[48] = "";
        if (p.memberSince > 0) { std::time_t t = (std::time_t)p.memberSince; if (std::tm* tm = std::localtime(&t)) std::strftime(since, sizeof(since), "%B %d, %Y", tm); }
        row(1, "FAVOURITE GAME", p.favoriteGame);
        row(0, "LOCATION", p.location);
        row(2, "MEMBER SINCE", since);
        if (!p.online && p.lastSeen > 0) row(3, "LAST ONLINE", RelTime(p.lastSeen));
        endCard();
    };

    // Hours per game + time in the editor.
    auto playtimeCard = [&](float w) {
        if (playAll.empty()) return;
        beginCard("##pfplay", w);
        char right[48]; std::snprintf(right, sizeof(right), "%s total", HoursText(playSecs + editorSecs).c_str());
        cardTitle("PLAYTIME", right, false);
        ImDrawList* d = ImGui::GetWindowDrawList();
        long top = 1; for (const auto& e : playAll) top = std::max(top, e.seconds);
        const float cw = ImGui::GetContentRegionAvail().x;
        int n = 0;
        for (const auto& e : playAll) {
            if (n++ >= 6) break;
            const bool ed = e.key == "editor";
            ImVec2 rp = ImGui::GetCursorScreenPos();
            const float rh = 40.0f;
            // icon: a pad for games, a wrench-ish square for the editor
            const ImVec2 ic(rp.x + 16, rp.y + rh * 0.5f);
            d->AddRectFilled(ImVec2(ic.x - 15, ic.y - 15), ImVec2(ic.x + 15, ic.y + 15), ed ? IM_COL32(130, 210, 120, 40) : IM_COL32(100, 170, 255, 40), 8.0f);
            DrawNavIcon(d, ed ? 0 : 1, ic, ed ? IM_COL32(130, 210, 120, 255) : IM_COL32(120, 180, 255, 255));
            const float tx = rp.x + 42, tw2 = cw - 42;
            d->AddText(ImVec2(tx, rp.y + 2), cText, FitText(ed ? std::string("In the editor") : e.title, tw2 - 90).c_str());
            const std::string hrs = HoursText(e.seconds);
            const float hw = ImGui::CalcTextSize(hrs.c_str()).x;
            d->AddText(ImVec2(rp.x + cw - hw, rp.y + 2), cText, hrs.c_str());
            const float by = rp.y + 24, frac = (float)e.seconds / (float)top;
            d->AddRectFilled(ImVec2(tx, by), ImVec2(tx + tw2 - 100, by + 5), IM_COL32(255, 255, 255, 18), 3.0f);
            d->AddRectFilled(ImVec2(tx, by), ImVec2(tx + std::max(4.0f, (tw2 - 100) * frac), by + 5),
                             ed ? IM_COL32(130, 210, 120, 255) : ImGui::GetColorU32(kAccent), 3.0f);
            if (e.lastPlayed > 0) {
                const std::string lp = (PlaytimeRunning(e.key) && me ? std::string("now") : RelTime(e.lastPlayed));
                const float lw = ImGui::CalcTextSize(lp.c_str()).x;
                d->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.85f, ImVec2(rp.x + cw - lw * 0.85f, by - 3), cDim, lp.c_str());
            }
            ImGui::Dummy(ImVec2(cw, rh));
        }
        endCard();
    };
    // Badges: staff-awarded ones and those earned from what they've done.
    auto badgesCard = [&](float w) {
        const auto badges = ProfileBadges(s_b, achN, achPts, gameN, playSecs, editorSecs);
        if (badges.empty() && !p.verified) return;
        beginCard("##pfbadges", w);
        cardTitle("BADGES", nullptr, false);
        ImDrawList* d = ImGui::GetWindowDrawList();
        const float cw = ImGui::GetContentRegionAvail().x, bs = 40.0f, g = 8.0f;
        const int cols = std::max(1, (int)((cw + g) / (bs + g)));
        ImVec2 o = ImGui::GetCursorScreenPos();
        std::vector<ProfileBadge> all = badges;
        if (p.verified) all.insert(all.begin(), ProfileBadge{"Verified", "A verified OkaySpace account.", IM_COL32(56, 151, 240, 255), 'v'});
        for (std::size_t i = 0; i < all.size(); ++i) {
            const ProfileBadge& bd = all[i];
            const ImVec2 bp(o.x + (i % cols) * (bs + g), o.y + (i / cols) * (bs + g));
            const ImVec2 c(bp.x + bs * 0.5f, bp.y + bs * 0.5f);
            if (bd.glyph == 'v') DrawVerifiedBadge(d, c, bs * 0.42f);
            else {
                d->AddNgonFilled(c, bs * 0.46f, (bd.color & 0x00FFFFFF) | 0x40000000, 6);   // tinted fill
                d->AddNgon(c, bs * 0.46f, bd.color, 6, 2.0f);
                char gl[2] = {bd.glyph, 0};
                ImGui::SetWindowFontScale(1.1f);
                const ImVec2 gs = ImGui::CalcTextSize(gl);
                d->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(c.x - gs.x * 0.5f, c.y - gs.y * 0.5f), bd.color, gl);
                ImGui::SetWindowFontScale(1.0f);
            }
            if (ImGui::IsMouseHoveringRect(bp, ImVec2(bp.x + bs, bp.y + bs)) && ImGui::IsWindowHovered())
                ImGui::SetTooltip("%s\n%s", bd.name.c_str(), bd.desc.c_str());
        }
        const int rows = ((int)all.size() + cols - 1) / cols;
        ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rows * (bs + g) - g));
        ImGui::Dummy(ImVec2(0, 0));
        endCard();
    };
    // Social links as pills (click to open; Discord copies the tag).
    auto linksCard = [&](float w) {
        if (p.socials.empty()) return;
        beginCard("##pflinks", w);
        cardTitle("LINKS", nullptr, false);
        ImDrawList* d = ImGui::GetWindowDrawList();
        const float cw = ImGui::GetContentRegionAvail().x;
        ImVec2 o = ImGui::GetCursorScreenPos();
        float x = o.x, y = o.y;
        const float ph = ImGui::GetTextLineHeight() + 12.0f;
        for (const auto& kv : p.socials) {
            const SocialPlatform* sp = SocialFor(kv.first);
            if (!sp) continue;
            std::string shown = kv.first == "website" ? kv.second.substr(kv.second.find("://") + 3) : (kv.first == "discord" ? kv.second : "@" + kv.second);
            shown = FitText(shown, cw - 90);
            const float pw = 22.0f + ImGui::CalcTextSize(sp->label).x + 8 + ImGui::CalcTextSize(shown.c_str()).x + 14;
            if (x > o.x && x + pw > o.x + cw) { x = o.x; y += ph + 6; }
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            ImGui::PushID(kv.first.c_str());
            const bool hit = ImGui::InvisibleButton("##soc", ImVec2(pw, ph));
            const bool h = ImGui::IsItemHovered();
            ImGui::PopID();
            d->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + ph), h ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 12), ph * 0.5f);
            d->AddCircleFilled(ImVec2(x + 13, y + ph * 0.5f), 5.0f, sp->color, 12);
            d->AddText(ImVec2(x + 22, y + 6), cText, sp->label);
            d->AddText(ImVec2(x + 22 + ImGui::CalcTextSize(sp->label).x + 8, y + 6), cDim, shown.c_str());
            const std::string url = SocialUrl(kv.first, kv.second);
            if (h) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", url.empty() ? "Copy" : url.c_str()); }
            if (hit) {
                if (!url.empty()) OpenExternal(url);
                else { ImGui::SetClipboardText(kv.second.c_str()); Toast(std::string(sp->label) + " copied", 1); }
            }
            x += pw + 6;
        }
        ImGui::SetCursorScreenPos(ImVec2(o.x, y + ph));
        ImGui::Dummy(ImVec2(0, 0));
        endCard();
    };

    auto activityCard = [&](float w) {
        beginCard("##pfactivity", w);
        cardTitle("RECENT ACTIVITY", nullptr, false);
        if (s_b.activity.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            if (!s_loaded && !kDemo) ImGui::TextDisabled("Loading\xE2\x80\xA6");
            else if (!me && s_b.friendStatus != "accepted") ImGui::TextDisabled("Recent activity is visible to friends.");
            else ImGui::TextDisabled("Nothing new lately.");
            ImGui::PopTextWrapPos();
        }
        int n = 0;
        for (const auto& e : s_b.activity) {
            if (n++ >= 6) break;
            ImVec2 ip = ImGui::GetCursorScreenPos();
            ImDrawList* d = ImGui::GetWindowDrawList();
            ImVec2 c(ip.x + 15.0f, ip.y + 15.0f);
            int icon = e.kind == "playing" ? 1 : e.kind == "achievement" ? 5 : e.kind == "publish" ? 7 : 3;
            d->AddCircleFilled(c, 15.0f, e.kind == "achievement" ? IM_COL32(240, 196, 92, 40) : IM_COL32(255, 255, 255, 14), 28);
            DrawNavIcon(d, icon, c, e.kind == "achievement" ? IM_COL32(240, 196, 92, 255) : cText);
            const char* verb = e.kind == "playing" ? "Played " : e.kind == "achievement" ? "Unlocked "
                             : e.kind == "publish" ? "Published " : e.kind == "online" ? "Came online" : "";
            ImGui::SetCursorScreenPos(ImVec2(ip.x + 40.0f, ip.y));
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("%s%s", verb, e.detail.c_str());
            ImGui::PopTextWrapPos();
            std::string ago = RelTime(e.when);
            if (!ago.empty()) ImGui::TextDisabled("%s", ago.c_str());
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(0, 6));
        }
        endCard();
    };

    ImGui::BeginGroup();
    if (s_edit) editCard(mainW);
    if (!twoCol) { aboutCard(mainW); badgesCard(mainW); linksCard(mainW); }
    achCard(mainW);
    playtimeCard(mainW);
    gamesCard(mainW);
    if (!twoCol) activityCard(mainW);
    ImGui::EndGroup();
    if (twoCol) {
        ImGui::SameLine(0, gap);
        ImGui::BeginGroup();
        aboutCard(sideW);
        badgesCard(sideW);
        linksCard(sideW);
        activityCard(sideW);
        ImGui::EndGroup();
    }
    ImGui::EndChild();   // ##pfbody
    return out;
}

} // namespace okl
