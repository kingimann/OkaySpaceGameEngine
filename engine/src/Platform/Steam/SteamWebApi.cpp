// The Store Kit's Steam Web API checks (okay/Platform/Steam/SteamWebApi.hpp). Our own code from Valve's public Web API
// documentation; a small JSON reader of our own for the one response it reads.
#include "okay/Platform/Steam/SteamWebApi.hpp"
#include "okay/Platform/NativeHttp.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace okay::steamweb {

std::string SchemaUrl(std::uint32_t appId) {
    return "https://api.steampowered.com/ISteamUserStats/GetSchemaForGame/v2/?appid=" + std::to_string(appId) + "&format=json";
}
std::vector<std::string> SchemaHeaders(const std::string& key) { return {"x-webapi-key: " + key, "Accept: application/json"}; }

namespace {
// ---- a small JSON reader: walks values, collects what we need --------------------------------------------------
struct Reader {
    const std::string& s; std::size_t i = 0; bool bad = false;
    explicit Reader(const std::string& t) : s(t) {}
    void Ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool Eat(char c) { Ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    std::string Str() {
        std::string o;
        if (!Eat('"')) { bad = true; return o; }
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                const char e = s[++i];
                if (e == 'u' && i + 4 < s.size()) { const unsigned v = (unsigned)std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16); if (v < 128) o.push_back((char)v); else o.push_back('?'); i += 4; }
                else o.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'b' ? '\b' : e == 'f' ? '\f' : e);
                ++i;
            } else o.push_back(s[i++]);
        }
        if (i >= s.size()) bad = true; else ++i;
        return o;
    }
    // Read any value. For an object, `onKey(key)` is called before each member's value; return true to have the
    // caller read the value itself (then it must), false to skip it.
    void Skip(int depth = 0) {
        Ws();
        if (i >= s.size() || depth > 64) { bad = true; return; }
        if (s[i] == '"') { Str(); return; }
        if (s[i] == '{') { ++i; if (Eat('}')) return; do { Str(); if (!Eat(':')) { bad = true; return; } Skip(depth + 1); } while (!bad && Eat(',')); if (!Eat('}')) bad = true; return; }
        if (s[i] == '[') { ++i; if (Eat(']')) return; do { Skip(depth + 1); } while (!bad && Eat(',')); if (!Eat(']')) bad = true; return; }
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.')) ++i;
    }
    // An array of objects: the "name" member of each.
    void Names(std::vector<std::string>& out) {
        if (!Eat('[')) { Skip(); return; }
        if (Eat(']')) return;
        do {
            Ws();
            if (i < s.size() && s[i] == '{') {
                ++i;
                if (!Eat('}')) {
                    do { const std::string k = Str(); if (!Eat(':')) { bad = true; return; }
                         Ws(); if (k == "name" && i < s.size() && s[i] == '"') out.push_back(Str()); else Skip(); } while (!bad && Eat(','));
                    if (!Eat('}')) bad = true;
                }
            } else Skip();
        } while (!bad && Eat(','));
        if (!Eat(']')) bad = true;
    }
};

std::vector<std::string> Unique(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}
std::vector<std::string> Matches(const std::string& text, const std::regex& re) {
    std::vector<std::string> out;
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) out.push_back((*it)[1].str());
    return out;
}
}  // namespace

Schema ParseSchema(const std::string& json) {
    Schema sc;
    Reader r(json);
    if (!r.Eat('{')) { sc.error = "The answer from Steam was not readable."; return sc; }
    bool sawGame = false;
    if (!r.Eat('}')) do {
        const std::string k = r.Str();
        if (!r.Eat(':')) { r.bad = true; break; }
        if (k != "game") { r.Skip(); continue; }
        sawGame = true;
        if (!r.Eat('{')) { r.Skip(); continue; }
        if (!r.Eat('}')) do {
            const std::string gk = r.Str();
            if (!r.Eat(':')) { r.bad = true; break; }
            r.Ws();
            if (gk == "gameName" && r.i < json.size() && json[r.i] == '"') sc.gameName = r.Str();
            else if (gk == "availableGameStats" && r.Eat('{')) {
                if (!r.Eat('}')) do {
                    const std::string sk = r.Str();
                    if (!r.Eat(':')) { r.bad = true; break; }
                    if (sk == "achievements") r.Names(sc.achievements);
                    else if (sk == "stats") r.Names(sc.stats);
                    else r.Skip();
                } while (!r.bad && r.Eat(','));
                if (!r.bad && !r.Eat('}')) r.bad = true;
            } else r.Skip();
        } while (!r.bad && r.Eat(','));
        if (!r.bad && !r.Eat('}')) r.bad = true;
    } while (!r.bad && r.Eat(','));
    if (r.bad) { sc.error = "The answer from Steam was not readable."; return sc; }
    if (!sawGame) { sc.error = "Steam sent no game information (is the app id right?)."; return sc; }
    sc.ok = true;
    return sc;
}

std::vector<std::string> FindAchievementIds(const std::string& text) {
    static const std::regex call("(?:Achievements\\.(?:Unlock|IsUnlocked|Progress)|steam_unlock|steam_is_unlocked|steam_progress|steam_clear)\\s*\\(\\s*\"([^\"\\n]{1,128})\"");
    static const std::regex node("node\\s+\\d+\\s+Steam(?:Unlock|IsUnlocked)\\s+([^\\s]{1,128})");
    std::vector<std::string> v = Matches(text, call), n = Matches(text, node);
    v.insert(v.end(), n.begin(), n.end());
    return Unique(v);
}

std::vector<std::string> FindStatNames(const std::string& text) {
    static const std::regex call("(?:Stats\\.(?:Set|Add|Get)|steam_(?:set|get|inc)_stat(?:_int)?)\\s*\\(\\s*\"([^\"\\n]{1,128})\"");
    return Unique(Matches(text, call));
}

void ScanProject(const std::string& projectDir, std::vector<std::string>& achievements, std::vector<std::string>& stats) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (projectDir.empty() || !fs::is_directory(projectDir, ec)) return;
    int files = 0;
    for (fs::recursive_directory_iterator it(projectDir, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::path& p = it->path();
        const std::string name = p.filename().string();
        if (it->is_directory(ec) && (name == ".okay" || name == ".git" || name == "Builds" || name == "build")) { it.disable_recursion_pending(); continue; }
        const std::string ext = p.extension().string();
        if (ext != ".okayscript" && ext != ".os" && ext != ".okayvs" && ext != ".okayscene") continue;
        if (++files > 5000) break;
        std::error_code se;
        if (fs::file_size(p, se) > (8u << 20)) continue;
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        const std::string t = ss.str();
        for (auto& a : FindAchievementIds(t)) achievements.push_back(a);
        for (auto& s : FindStatNames(t)) stats.push_back(s);
    }
    achievements = Unique(achievements);
    stats = Unique(stats);
}

std::vector<std::string> Compare(const Schema& schema, const std::vector<std::string>& usedAchievements, const std::vector<std::string>& usedStats) {
    std::vector<std::string> out;
    const std::set<std::string> a(schema.achievements.begin(), schema.achievements.end()), s(schema.stats.begin(), schema.stats.end());
    for (const auto& id : usedAchievements)
        if (!a.count(id)) out.push_back("Achievement \"" + id + "\" is used in a script but is not defined on Steam (Steamworks > Stats & Achievements).");
    for (const auto& id : usedStats)
        if (!s.count(id)) out.push_back("Stat \"" + id + "\" is used in a script but is not defined on Steam.");
    return out;
}

Schema FetchSchema(std::uint32_t appId, const std::string& key,
                   const std::function<bool(const std::string&, const std::vector<std::string>&, int*, std::string*)>& get) {
    Schema sc;
    if (appId == 0) { sc.error = "Type the Steam app id first."; return sc; }
    if (key.empty()) { sc.error = std::string("Set the environment variable ") + KeyEnv() + " to your publisher Web API key first (Steamworks > Users & Permissions > Manage Groups). It is never saved."; return sc; }
    int status = 0; std::string body;
    if (!get || !get(SchemaUrl(appId), SchemaHeaders(key), &status, &body)) { sc.error = "Could not reach Steam (api.steampowered.com)."; return sc; }
    if (status == 401 || status == 403) { sc.error = "Steam refused the Web API key (HTTP " + std::to_string(status) + "). Check that it is a publisher key for this app."; return sc; }
    if (status != 200) { sc.error = "Steam answered HTTP " + std::to_string(status) + "."; return sc; }
    sc = ParseSchema(body);
    return sc;
}

Schema FetchSchemaOnline(std::uint32_t appId, const std::string& key) {
    return FetchSchema(appId, key, [](const std::string& url, const std::vector<std::string>& headers, int* status, std::string* body) {
        const okay::http::Result r = okay::http::Request("GET", url, headers, std::string(), std::string(), std::string(), 15000);
        if (!r.reached) return false;
        *status = (int)r.status; *body = r.body;
        return true;
    });
}

}  // namespace okay::steamweb
