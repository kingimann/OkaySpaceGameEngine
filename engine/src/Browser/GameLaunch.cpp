// Browser: running a page's OkaySpace game natively (see okay/Browser/GameLaunch.hpp).
#include "okay/Browser/GameLaunch.hpp"

#include "okay/Browser/Url.hpp"
#include "okay/Core/Json.hpp"
#include "okay/Core/StringId.hpp"
#include "okay/Core/StringUtil.hpp"

#include <cstdio>

namespace okay::browser {

namespace {

constexpr std::size_t kMaxFolderName = 80;

// Letters, digits, '.', '-' and '_' stay (lower-cased); every other run of bytes becomes one '_'. Never "", "." or "..".
std::string CleanName(const std::string& s) {
    std::string o;
    for (char ch : str::LowerAscii(s)) {
        const bool keep = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_';
        if (keep) o += ch;
        else if (o.empty() || o.back() != '_') o += '_';
    }
    while (!o.empty() && (o.front() == '.' || o.front() == '_')) o.erase(o.begin());
    while (!o.empty() && (o.back() == '.' || o.back() == '_')) o.pop_back();
    return o;
}

std::string Hex8(const std::string& s) {
    char b[17];
    std::snprintf(b, sizeof b, "%08llx", (unsigned long long)(StringId::HashOf(s) & 0xffffffffull));
    return b;
}

void CollectScripts(const Node& n, std::vector<const Node*>& out) {
    if (n.kind == NodeKind::Element && n.name == "script") out.push_back(&n);
    for (const auto& c : n.children) CollectScripts(*c, out);
}

bool IsHttp(const std::string& url) {
    const Url u = ParseUrl(url);
    return (u.scheme == "http" || u.scheme == "https") && u.hasAuthority && !HostOf(url).empty();
}

} // namespace

WebExportConfig FindWebExportConfig(const HtmlDocument& doc) {
    WebExportConfig cfg;
    if (!doc.root) return cfg;
    std::vector<const Node*> scripts;
    CollectScripts(*doc.root, scripts);
    static const std::string kMark = "OKAY_WEB_CONFIG";
    for (const Node* s : scripts) {
        const std::string text = s->TextContent();
        const std::size_t at = text.find(kMark);
        if (at == std::string::npos) continue;
        std::size_t i = at + kMark.size();
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
        if (i >= text.size() || text[i] != '=') continue;
        ++i;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
        if (i >= text.size() || text[i] != '{') continue;
        // The object ends at the matching '}' (outside strings); the JSON reader then checks it properly.
        int depth = 0;
        bool inStr = false, esc = false;
        std::size_t end = std::string::npos;
        for (std::size_t k = i; k < text.size(); ++k) {
            const char c = text[k];
            if (inStr) { if (esc) esc = false; else if (c == '\\') esc = true; else if (c == '"') inStr = false; continue; }
            if (c == '"') inStr = true;
            else if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) { end = k + 1; break; }
        }
        if (end == std::string::npos) continue;
        json::Value v;
        json::Limits lim;
        lim.maxBytes = 1u << 20;
        lim.maxDepth = 16;
        if (!json::Parse(std::string_view(text).substr(i, end - i), v, nullptr, lim) || !v.IsObject()) continue;
        if (const json::Value* t = v.Get("title")) if (t->IsString()) cfg.title = t->string;
        if (const json::Value* g = v.Get("gameId")) if (g->IsString()) cfg.gameId = g->string;
        if (const json::Value* p = v.Get("packs"); p && p->IsArray())
            for (const json::Value& e : p->array)
                if (const json::Value* f = e.Get("file"); f && f->IsString() && !f->string.empty()) cfg.packs.push_back(f->string);
        cfg.found = true;
        return cfg;
    }
    return cfg;
}

std::string GamePackUrl(const std::string& pageUrl, const std::string& tag, const std::string& src, const WebExportConfig& config) {
    std::string ref;
    if (tag == "okay-game") ref = src;
    else if (tag == "canvas" && config.found && !config.packs.empty()) ref = config.packs.front();
    if (ref.empty() || !IsHttp(pageUrl)) return std::string();
    const std::string url = WithoutFragment(ResolveUrl(pageUrl, ref));
    if (!IsHttp(url) || !SameOrigin(url, pageUrl)) return std::string();
    return url;
}

std::string SiteFolderName(const std::string& origin) {
    std::string name = CleanName(origin);
    if (name.empty()) name = "site";
    if (name.size() > kMaxFolderName) name = name.substr(0, kMaxFolderName - 9) + "_" + Hex8(origin);
    return name;
}

std::string GameFolderName(const std::string& packUrl) {
    std::string path = ParseUrl(packUrl).path;
    const std::size_t slash = path.find_last_of('/');
    std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    for (const char* ext : {".gz", ".okaypack"}) {
        const std::string e = ext;
        if (file.size() > e.size() && str::LowerAscii(file.substr(file.size() - e.size())) == e) file.resize(file.size() - e.size());
    }
    std::string name = CleanName(file);
    if (name.empty()) name = "game";
    if (name.size() > 40) name.resize(40);
    while (!name.empty() && (name.back() == '.' || name.back() == '_')) name.pop_back();
    return name + "_" + Hex8(WithoutFragment(packUrl));
}

GameLaunchPlan MakeGameLaunchPlan(const std::string& dataRoot, const std::string& playerExe, const std::string& packUrl) {
    GameLaunchPlan p;
    std::string root = dataRoot;
    if (!root.empty() && root.back() != '/' && root.back() != '\\') root += '/';
    const std::string game = GameFolderName(packUrl);
    p.siteDir = root + "sites/" + SiteFolderName(OriginOf(packUrl)) + "/";
    p.gameDir = p.siteDir + "games/" + game + "/";
    p.saveDir = p.siteDir + "saves/" + game + "/";
    p.downloadPath = p.siteDir + "games/" + game + ".download";
    p.argv = {playerExe, "--game-dir=" + p.gameDir, "--save-dir=" + p.saveDir};
    return p;
}

} // namespace okay::browser
