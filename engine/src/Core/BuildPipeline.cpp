#include "okay/Core/BuildPipeline.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Core/ImageWrite.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace okay {
namespace buildpipe {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string ExtOf(const std::string& p) {
    const std::size_t slash = p.find_last_of("/\\");
    const std::size_t dot = p.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return std::string();
    return Lower(p.substr(dot + 1));
}

std::string Generic(const fs::path& p) { return p.generic_string(); }

bool ReadTextFile(const std::string& path, std::string& out, std::uintmax_t limit) {
    std::error_code ec;
    const std::uintmax_t sz = fs::file_size(fs::u8path(path), ec);
    if (ec || sz > limit) return false;
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string CanonicalKey(const std::string& path) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(fs::u8path(path), ec);
    return ec ? Generic(fs::u8path(path)) : Generic(c);
}

bool IsReservedDeviceName(const std::string& n) {
    static const char* names[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                  "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    std::string u;
    for (char c : n) u += (char)std::toupper((unsigned char)c);
    for (const char* r : names) if (u == r) return true;
    return false;
}

} // namespace

// ---- layout ----------------------------------------------------------------------------------------

const char* LayoutName(Layout l) {
    switch (l) {
        case Layout::Folder: return "folder";
        case Layout::Compact: return "compact";
        case Layout::Zip: return "zip";
    }
    return "folder";
}

bool ParseLayout(const std::string& text, Layout& out) {
    const std::string t = Lower(text);
    if (t == "folder") { out = Layout::Folder; return true; }
    if (t == "compact" || t == "single" || t == "legacy") { out = Layout::Compact; return true; }
    if (t == "zip") { out = Layout::Zip; return true; }
    return false;
}

std::string FileSafeName(const std::string& name, const std::string& fallback) {
    std::string out;
    for (unsigned char c : name) {
        if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') out += '_';
        else out += (char)c;
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    std::size_t lead = 0;
    while (lead < out.size() && out[lead] == ' ') ++lead;
    out.erase(0, lead);
    if (out.empty() || IsReservedDeviceName(out)) {
        if (out.empty() && fallback.empty()) return "Game";
        return out.empty() ? fallback : out + "_";
    }
    return out;
}

std::string ExpandPattern(const std::string& pattern, const NameTokens& t) {
    const std::string pat = pattern.empty() ? std::string("{name}-{version}") : pattern;
    std::string out;
    for (std::size_t i = 0; i < pat.size();) {
        if (pat[i] == '{') {
            const std::size_t close = pat.find('}', i);
            if (close != std::string::npos) {
                const std::string key = Lower(pat.substr(i + 1, close - i - 1));
                std::string val;
                bool known = true;
                if (key == "name") val = t.name;
                else if (key == "version") val = t.version;
                else if (key == "build") val = std::to_string(t.build);
                else if (key == "platform") val = t.platform;
                else if (key == "date") val = t.date;
                else known = false;
                if (known) { out += val; i = close + 1; continue; }
            }
        }
        out += pat[i++];
    }
    return FileSafeName(out, "build");
}

LayoutPlan PlanLayout(const LayoutInput& in) {
    LayoutPlan p;
    p.requested = in.layout;
    p.effective = in.layout;
    p.safeName = FileSafeName(in.gameName, "Game");
    p.exeName = in.windows ? p.safeName + ".exe" : p.safeName;
    if (in.antiCheat && in.layout == Layout::Folder) {
        p.effective = Layout::Compact;
        p.notes.push_back("Anti-cheat checks Data/manifest.json, so this build uses the compact layout (exe + Data/).");
    }
    if (in.antiCheat && in.layout == Layout::Zip)
        p.notes.push_back("Anti-cheat checks Data/manifest.json, so the archive holds the compact layout (exe + Data/).");
    const bool compactInside = in.antiCheat;   // the archive's content layout
    switch (p.effective) {
        case Layout::Folder:
            p.dataDir = p.safeName + "_Data";
            break;
        case Layout::Compact:
            p.dataDir = in.compactDataFolder || in.antiCheat ? "Data" : "";
            break;
        case Layout::Zip:
            p.dataDir = compactInside ? "Data" : p.safeName + "_Data";
            p.makeZip = true;
            break;
    }
    p.keepFolder = !(p.effective == Layout::Zip) || in.keepFolderWithZip;
    if (p.effective == Layout::Zip) {
        p.zipName = ExpandPattern(in.archivePattern, in.tokens) + ".zip";
    }
    return p;
}

std::string FindDataDir(const std::string& baseDir, const std::string& exeStem) {
    std::error_code ec;
    const fs::path base = baseDir.empty() ? fs::path(".") : fs::u8path(baseDir);
    auto done = [&](const fs::path& d) {
        std::string s = Generic(d);
        if (s.empty() || s.back() != '/') s += '/';
        if (baseDir.empty() && s.rfind("./", 0) == 0) s.erase(0, 2);
        return s;
    };
    if (!exeStem.empty()) {
        const fs::path d = base / (exeStem + "_Data");
        if (fs::exists(d / "game.okayconfig", ec)) return done(d);
    }
    std::vector<fs::path> found;
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.size() > 5 && n.compare(n.size() - 5, 5, "_Data") == 0 && fs::exists(it->path() / "game.okayconfig", ec))
            found.push_back(it->path());
    }
    if (found.size() == 1) return done(found[0]);
    if (found.size() > 1 && !exeStem.empty()) {
        for (const fs::path& f : found)
            if (Lower(f.filename().string()) == Lower(exeStem + "_Data")) return done(f);
    }
    if (fs::exists(base / "Data" / "game.okayconfig", ec)) return done(base / "Data");
    return baseDir.empty() ? std::string() : done(base);
}

// ---- file kinds ------------------------------------------------------------------------------------

const char* KindName(FileKind k) {
    switch (k) {
        case FileKind::Executable: return "Executable";
        case FileKind::Library: return "Libraries";
        case FileKind::Scene: return "Scenes";
        case FileKind::Config: return "Config";
        case FileKind::Texture: return "Textures";
        case FileKind::Audio: return "Audio";
        case FileKind::Model: return "Models";
        case FileKind::Font: return "Fonts";
        case FileKind::Script: return "Scripts";
        case FileKind::Data: return "Data assets";
        case FileKind::Streaming: return "StreamingAssets";
        case FileKind::Other: return "Other";
    }
    return "Other";
}

FileKind KindOf(const std::string& relPath) {
    std::string p = relPath;
    for (char& c : p) if (c == '\\') c = '/';
    if (p.find("StreamingAssets/") == 0 || p.find("/StreamingAssets/") != std::string::npos) return FileKind::Streaming;
    const std::string e = ExtOf(p);
    static const std::set<std::string> tex = {"png", "jpg", "jpeg", "bmp", "tga", "gif", "hdr", "webp"};
    static const std::set<std::string> aud = {"wav", "ogg", "mp3", "flac", "mod", "xm", "it", "s3m"};
    static const std::set<std::string> mdl = {"obj", "fbx", "gltf", "glb", "dae", "okaymesh", "mtl", "okaymat"};
    static const std::set<std::string> fnt = {"ttf", "otf", "fnt", "ttc"};
    static const std::set<std::string> scr = {"okayvs", "okayscript", "lua", "cs", "okaybt"};
    static const std::set<std::string> dat = {"okaydata", "okayitems", "okayweapon", "okayloc", "okayprefab", "okayanim", "okayclips", "okaydialogue",
                                              "okayquests", "okayseq", "okaysequence", "json", "txt", "csv", "xml", "okaynav", "okayanimator"};
    if (e == "exe") return FileKind::Executable;
    if (e == "dll" || e == "so" || e == "dylib" || p.find("libSDL2") != std::string::npos) return FileKind::Library;
    if (e == "okayscene") return FileKind::Scene;
    if (e == "okayconfig" || e == "okayprefs") return FileKind::Config;
    if (tex.count(e)) return FileKind::Texture;
    if (aud.count(e)) return FileKind::Audio;
    if (mdl.count(e)) return FileKind::Model;
    if (fnt.count(e)) return FileKind::Font;
    if (scr.count(e)) return FileKind::Script;
    if (dat.count(e) || e == "okaymeta") return FileKind::Data;
    if (e.empty() && p.find('/') == std::string::npos) return FileKind::Executable;   // a Linux player: no extension, at the root
    return FileKind::Other;
}

bool IsEditorOnlyFile(const std::string& relPath) {
    static const std::set<std::string> ed = {"bak", "tmp", "swp", "part", "corrupt", "psd", "blend", "blend1", "kra", "xcf", "ase",
                                             "aseprite", "pdn", "clip", "sketch", "afdesign", "orig", "rej", "log"};
    const std::string e = ExtOf(relPath);
    if (ed.count(e)) return true;
    const std::size_t slash = relPath.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? relPath : relPath.substr(slash + 1);
    return name == ".DS_Store" || name == "Thumbs.db" || (!name.empty() && name.back() == '~');
}

// ---- the dependency walk ---------------------------------------------------------------------------

namespace {
bool IsAbsolutePath(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    return p.size() >= 2 && p[1] == ':' && std::isalpha((unsigned char)p[0]);
}
bool HasDotDot(const std::string& ref) {
    std::string r = ref;
    for (char& c : r) if (c == '\\') c = '/';
    std::size_t i = 0;
    while (i <= r.size()) {
        std::size_t j = r.find('/', i);
        if (j == std::string::npos) j = r.size();
        if (r.compare(i, j - i, "..") == 0 && j - i == 2) return true;
        i = j + 1;
    }
    return false;
}
bool IsFile(const fs::path& p) { std::error_code ec; return fs::is_regular_file(p, ec); }
} // namespace

std::string ResolveAssetFile(const std::string& ref, const std::string& referrerDir, const WalkSettings& s) {
    if (ref.empty()) return std::string();
    const fs::path r = fs::u8path(ref);
    if (IsFile(r)) return Generic(r);
    if (IsAbsolutePath(ref) || HasDotDot(ref)) return std::string();
    auto tryDir = [&](const std::string& dir) -> std::string {
        if (dir.empty()) return std::string();
        const fs::path c = fs::u8path(dir) / r;
        return IsFile(c) ? Generic(c) : std::string();
    };
    std::string hit;
    if (!s.projectDir.empty()) {
        if (!(hit = tryDir(s.projectDir)).empty()) return hit;
        if (!(hit = tryDir(Generic(fs::u8path(s.projectDir) / "Assets"))).empty()) return hit;
    }
    if (!(hit = tryDir(referrerDir)).empty()) return hit;
    for (const std::string& d : s.searchDirs) if (!(hit = tryDir(d)).empty()) return hit;
    return std::string();
}

std::string DestinationFor(const std::string& ref) {
    if (IsAbsolutePath(ref)) {
        std::string n = fs::u8path(ref).filename().generic_string();
        return n;
    }
    std::string r = ref;
    for (char& c : r) if (c == '\\') c = '/';
    std::string out;
    std::size_t i = 0;
    while (i <= r.size()) {
        std::size_t j = r.find('/', i);
        if (j == std::string::npos) j = r.size();
        const std::string part = r.substr(i, j - i);
        if (!part.empty() && part != "." && part != "..") { if (!out.empty()) out += '/'; out += part; }
        i = j + 1;
    }
    return out;
}

namespace {
// Which assets are read for the files they use, and how.
enum class Scan { None, Scene, Material, Text };
Scan ScanKindFor(const std::string& ext) {
    if (ext == "okayscene" || ext == "okayprefab") return Scan::Scene;
    if (ext == "okaymat") return Scan::Material;
    static const std::set<std::string> text = {"okayanim", "okayclips", "okaydialogue", "okayquests", "okaybt", "okayseq", "okaysequence",
                                               "okayitems", "okayweapon", "okaydata", "okaynav", "okayanimator", "okaymesh", "gltf", "mtl", "obj",
                                               "json", "okayloc"};
    return text.count(ext) ? Scan::Text : Scan::None;
}
} // namespace

WalkResult WalkAssets(const std::vector<AssetRef>& roots, const WalkSettings& s) {
    WalkResult res;
    std::set<std::string> seenSrc, seenMissing, seenAbs;
    struct Item { std::string ref, referrer; };
    std::vector<Item> queue;
    for (const AssetRef& r : roots) queue.push_back({r.ref, r.referrer});
    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
        const Item it = queue[qi];
        if (it.ref.empty() || it.ref[0] == '@') continue;            // '@name' = made in memory
        if (it.ref.find("://") != std::string::npos || it.ref.rfind("proc:", 0) == 0) continue;   // a URL or a built-in texture
        std::string referrerDir;
        if (!it.referrer.empty()) referrerDir = Generic(fs::u8path(it.referrer).parent_path());
        const std::string src = ResolveAssetFile(it.ref, referrerDir, s);
        if (src.empty()) {
            if (seenMissing.insert(it.ref).second) res.missing.push_back({it.ref, it.referrer});
            continue;
        }
        const std::string key = CanonicalKey(src);
        if (!seenSrc.insert(key).second) continue;
        ShippedAsset a;
        a.ref = it.ref;
        a.dest = DestinationFor(it.ref);
        if (a.dest.empty()) a.dest = fs::u8path(src).filename().generic_string();
        a.src = src;
        std::error_code ec;
        a.bytes = fs::file_size(fs::u8path(src), ec);
        if (ec) a.bytes = 0;
        a.reason = "used by " + (it.referrer.empty() ? std::string("the build") : fs::u8path(it.referrer).filename().generic_string());
        if (IsAbsolutePath(it.ref) && seenAbs.insert(it.ref).second) res.absolute.push_back(it.ref);
        res.assets.push_back(a);

        // What does this file use in turn?
        const Scan how = ScanKindFor(ExtOf(src));
        if (how == Scan::None) continue;
        std::string text;
        if (!ReadTextFile(src, text, s.maxScanBytes)) continue;
        std::vector<FoundRef> found = how == Scan::Scene ? ExtractSceneRefs(text) : how == Scan::Material ? ExtractMaterialRefs(text) : ExtractTextRefs(text);
        for (const FoundRef& f : found)
            if (!f.path.empty() && f.path != "-") queue.push_back({f.path, src});
    }
    return res;
}

std::vector<std::string> ScriptRefsInSceneText(const std::string& sceneText) {
    std::vector<std::string> out;
    for (const FoundRef& f : ExtractSceneRefs(sceneText))
        if (ExtOf(f.path) == "okayvs" && std::find(out.begin(), out.end(), f.path) == out.end()) out.push_back(f.path);
    return out;
}

std::vector<ProjectAsset> ListProjectAssets(const std::string& projectDir) {
    std::vector<ProjectAsset> out;
    if (projectDir.empty()) return out;
    const fs::path root = fs::u8path(projectDir) / "Assets";
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        const std::string name = p.filename().string();
        if (it->is_directory(ec)) { if (!name.empty() && name[0] == '.') it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec)) continue;
        if (ExtOf(name) == "okaymeta") continue;
        ProjectAsset a;
        a.rel = Generic(p.lexically_relative(root));
        a.src = Generic(p);
        std::error_code se;
        a.bytes = fs::file_size(p, se);
        a.editorOnly = IsEditorOnlyFile(a.rel);
        a.scene = ExtOf(name) == "okayscene";
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const ProjectAsset& a, const ProjectAsset& b) { return a.rel < b.rel; });
    return out;
}

std::vector<std::pair<std::string, std::string>> ListStreamingAssets(const std::string& projectDir) {
    std::vector<std::pair<std::string, std::string>> out;
    if (projectDir.empty()) return out;
    const fs::path root = fs::u8path(projectDir) / "StreamingAssets";
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        out.emplace_back(Generic(it->path().lexically_relative(root)), Generic(it->path()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<ExcludedFile> FindUnshipped(const std::string& projectDir, const std::vector<std::string>& shippedSources,
                                        const std::vector<std::string>& sceneSources) {
    std::vector<ExcludedFile> out;
    std::set<std::string> shipped, scenes;
    for (const std::string& s : shippedSources) shipped.insert(CanonicalKey(s));
    for (const std::string& s : sceneSources) scenes.insert(CanonicalKey(s));
    for (const ProjectAsset& a : ListProjectAssets(projectDir)) {
        const std::string key = CanonicalKey(a.src);
        if (shipped.count(key) || scenes.count(key)) continue;
        ExcludedFile e;
        e.path = "Assets/" + a.rel;
        e.bytes = a.bytes;
        e.reason = a.editorOnly ? "editor-only file" : a.scene ? "scene is not in the build" : "not used by any scene in the build";
        out.push_back(e);
    }
    return out;
}

// ---- the manifest ----------------------------------------------------------------------------------

std::uint64_t HashText(const std::string& text) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ull; }
    return h;
}
std::string HashHex(std::uint64_t h) {
    char b[24];
    std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)h);
    return b;
}
std::string UtcNowIso() {
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
    if (const std::tm* g = std::gmtime(&t)) tmv = *g;
    char b[32];
    std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return b;
}

namespace {
std::string JsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 32) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

// A tiny reader for the flat object ManifestToJson writes: "key": string | number | true/false | ["strings"].
struct JsonValue { enum { Str, Num, Bool, Arr } kind = Str; std::string s; double n = 0; bool b = false; std::vector<std::string> a; };
struct JsonReader {
    const std::string& t;
    std::size_t i = 0;
    std::string err;
    explicit JsonReader(const std::string& text) : t(text) {}
    void Ws() { while (i < t.size() && std::isspace((unsigned char)t[i])) ++i; }
    bool Str(std::string& out) {
        Ws();
        if (i >= t.size() || t[i] != '"') return false;
        ++i; out.clear();
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c == '\\' && i < t.size()) {
                char e = t[i++];
                if (e == 'n') out += '\n'; else if (e == 'r') out += '\r'; else if (e == 't') out += '\t';
                else if (e == 'u' && i + 4 <= t.size()) { out += (char)std::strtol(t.substr(i, 4).c_str(), nullptr, 16); i += 4; }
                else out += e;
            } else out += c;
        }
        if (i >= t.size()) return false;
        ++i;
        return true;
    }
    bool Value(JsonValue& v) {
        Ws();
        if (i >= t.size()) return false;
        if (t[i] == '"') { v.kind = JsonValue::Str; return Str(v.s); }
        if (t[i] == '[') {
            ++i; v.kind = JsonValue::Arr; v.a.clear();
            for (;;) {
                Ws();
                if (i < t.size() && t[i] == ']') { ++i; return true; }
                std::string s;
                if (!Str(s)) return false;
                v.a.push_back(s);
                Ws();
                if (i < t.size() && t[i] == ',') ++i;
            }
        }
        if (t.compare(i, 4, "true") == 0) { v.kind = JsonValue::Bool; v.b = true; i += 4; return true; }
        if (t.compare(i, 5, "false") == 0) { v.kind = JsonValue::Bool; v.b = false; i += 5; return true; }
        char* end = nullptr;
        v.n = std::strtod(t.c_str() + i, &end);
        if (end == t.c_str() + i) return false;
        v.kind = JsonValue::Num;
        i = (std::size_t)(end - t.c_str());
        return true;
    }
};
} // namespace

std::string ManifestToJson(const BuildManifest& m) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"game\": \"" << JsonEscape(m.game) << "\",\n";
    o << "  \"company\": \"" << JsonEscape(m.company) << "\",\n";
    o << "  \"bundleId\": \"" << JsonEscape(m.bundleId) << "\",\n";
    o << "  \"version\": \"" << JsonEscape(m.version) << "\",\n";
    o << "  \"buildNumber\": " << m.buildNumber << ",\n";
    o << "  \"engineVersion\": \"" << JsonEscape(m.engineVersion) << "\",\n";
    o << "  \"date\": \"" << JsonEscape(m.dateUtc) << "\",\n";
    o << "  \"platform\": \"" << JsonEscape(m.platform) << "\",\n";
    o << "  \"layout\": \"" << JsonEscape(m.layout) << "\",\n";
    o << "  \"development\": " << (m.development ? "true" : "false") << ",\n";
    o << "  \"strippedAssets\": " << (m.stripped ? "true" : "false") << ",\n";
    o << "  \"compressed\": " << (m.compressed ? "true" : "false") << ",\n";
    o << "  \"settingsHash\": \"" << JsonEscape(m.settingsHash) << "\",\n";
    o << "  \"assetCount\": " << m.assetCount << ",\n";
    o << "  \"totalBytes\": " << m.totalBytes << ",\n";
    o << "  \"scenes\": [";
    for (std::size_t i = 0; i < m.scenes.size(); ++i) o << (i ? ", " : "") << "\"" << JsonEscape(m.scenes[i]) << "\"";
    o << "]\n}\n";
    return o.str();
}

bool ParseManifestJson(const std::string& text, BuildManifest& out, std::string* error) {
    JsonReader r(text);
    r.Ws();
    if (r.i >= text.size() || text[r.i] != '{') { if (error) *error = "not a JSON object"; return false; }
    ++r.i;
    BuildManifest m;
    bool any = false;
    for (;;) {
        r.Ws();
        if (r.i < text.size() && text[r.i] == '}') break;
        std::string key;
        if (!r.Str(key)) { if (error) *error = "expected a key"; return false; }
        r.Ws();
        if (r.i >= text.size() || text[r.i] != ':') { if (error) *error = "expected ':' after " + key; return false; }
        ++r.i;
        JsonValue v;
        if (!r.Value(v)) { if (error) *error = "bad value for " + key; return false; }
        any = true;
        if (key == "game") m.game = v.s;
        else if (key == "company") m.company = v.s;
        else if (key == "bundleId") m.bundleId = v.s;
        else if (key == "version") m.version = v.s;
        else if (key == "buildNumber") m.buildNumber = (int)v.n;
        else if (key == "engineVersion") m.engineVersion = v.s;
        else if (key == "date") m.dateUtc = v.s;
        else if (key == "platform") m.platform = v.s;
        else if (key == "layout") m.layout = v.s;
        else if (key == "development") m.development = v.b;
        else if (key == "strippedAssets") m.stripped = v.b;
        else if (key == "compressed") m.compressed = v.b;
        else if (key == "settingsHash") m.settingsHash = v.s;
        else if (key == "assetCount") m.assetCount = (int)v.n;
        else if (key == "totalBytes") m.totalBytes = (std::uintmax_t)v.n;
        else if (key == "scenes") m.scenes = v.a;
        r.Ws();
        if (r.i < text.size() && text[r.i] == ',') ++r.i;
    }
    if (!any) { if (error) *error = "empty manifest"; return false; }
    out = m;
    return true;
}

std::string ManifestSummary(const BuildManifest& m) {
    std::ostringstream o;
    o << m.game << " " << m.version << " (build " << m.buildNumber << "), engine " << m.engineVersion << ", " << m.dateUtc
      << ", " << m.platform << ", " << m.scenes.size() << (m.scenes.size() == 1 ? " scene" : " scenes")
      << (m.development ? ", development build" : "");
    return o.str();
}

// ---- the report ------------------------------------------------------------------------------------

std::string HumanBytes(std::uintmax_t b) {
    char s[32];
    double v = (double)b;
    const char* u = "B";
    if (v >= 1024.0 * 1024 * 1024) { v /= 1024.0 * 1024 * 1024; u = "GB"; }
    else if (v >= 1024.0 * 1024)   { v /= 1024.0 * 1024;        u = "MB"; }
    else if (v >= 1024.0)          { v /= 1024.0;               u = "KB"; }
    if (u[0] == 'B') std::snprintf(s, sizeof(s), "%llu B", (unsigned long long)b);
    else std::snprintf(s, sizeof(s), "%.1f %s", v, u);
    return s;
}

void Report::AddFile(const std::string& path, std::uintmax_t bytes, const std::string& reason) {
    ReportFile f;
    f.path = path; f.bytes = bytes; f.kind = KindOf(path); f.reason = reason;
    files.push_back(f);
}
void Report::AddExcluded(const std::string& path, std::uintmax_t bytes, const std::string& reason) {
    excluded.push_back({path, bytes, reason});
}
void Report::Finish() {
    total = 0; excludedBytes = 0;
    kindBytes.fill(0); kindCount.fill(0);
    std::stable_sort(files.begin(), files.end(), [](const ReportFile& a, const ReportFile& b) { return a.bytes != b.bytes ? a.bytes > b.bytes : a.path < b.path; });
    std::stable_sort(excluded.begin(), excluded.end(), [](const ExcludedFile& a, const ExcludedFile& b) { return a.bytes != b.bytes ? a.bytes > b.bytes : a.path < b.path; });
    for (const ReportFile& f : files) {
        total += f.bytes;
        kindBytes[(std::size_t)f.kind] += f.bytes;
        kindCount[(std::size_t)f.kind] += 1;
    }
    for (const ExcludedFile& e : excluded) excludedBytes += e.bytes;
    valid = true;
}
std::vector<Report::TypeRow> Report::ByExtension() const {
    std::map<std::string, TypeRow> m;
    for (const ReportFile& f : files) {
        std::string e = ExtOf(f.path);
        if (e.empty()) e = "(none)";
        TypeRow& r = m[e];
        r.ext = e; r.bytes += f.bytes; r.count += 1;
    }
    std::vector<TypeRow> out;
    for (auto& kv : m) out.push_back(kv.second);
    std::sort(out.begin(), out.end(), [](const TypeRow& a, const TypeRow& b) { return a.bytes != b.bytes ? a.bytes > b.bytes : a.ext < b.ext; });
    return out;
}
std::string Report::ToText(std::size_t maxRows) const {
    std::ostringstream o;
    o << (dryRun ? "Build report (dry run: nothing was written)" : "Build report") << "\n";
    o << "Layout: " << layout << "\n";
    if (!outputDir.empty()) o << "Output: " << outputDir << "\n";
    o << "Total: " << HumanBytes(total) << " in " << files.size() << " file" << (files.size() == 1 ? "" : "s") << "\n";
    if (!archivePath.empty())
        o << "Archive: " << archivePath << " " << HumanBytes(archiveBytes) << (archiveCompressed ? " (compressed, " : " (stored, ")
          << (total ? (int)(100.0 * (double)archiveBytes / (double)total + 0.5) : 100) << "% of the folder)\n";
    char tb[48];
    std::snprintf(tb, sizeof(tb), "%.2f s", seconds);
    o << "Build time: " << tb << "\n";
    o << "\nBy kind\n";
    for (int k = 0; k < kFileKindCount; ++k)
        if (kindCount[(std::size_t)k]) o << "  " << KindName((FileKind)k) << ": " << HumanBytes(kindBytes[(std::size_t)k]) << " (" << kindCount[(std::size_t)k] << ")\n";
    o << "\nBy type\n";
    for (const TypeRow& r : ByExtension()) o << "  " << (r.ext == "(none)" ? "" : ".") << r.ext << ": " << HumanBytes(r.bytes) << " (" << r.count << ")\n";
    o << "\nIncluded files, largest first\n";
    std::size_t n = 0;
    for (const ReportFile& f : files) {
        if (maxRows && n++ >= maxRows) { o << "  ... " << (files.size() - maxRows) << " more\n"; break; }
        o << "  " << HumanBytes(f.bytes) << "  " << f.path;
        if (!f.reason.empty()) o << "  (" << f.reason << ")";
        o << "\n";
    }
    o << "\nExcluded from the build: " << excluded.size() << " file" << (excluded.size() == 1 ? "" : "s") << ", " << HumanBytes(excludedBytes) << "\n";
    n = 0;
    for (const ExcludedFile& e : excluded) {
        if (maxRows && n++ >= maxRows) { o << "  ... " << (excluded.size() - maxRows) << " more\n"; break; }
        o << "  " << HumanBytes(e.bytes) << "  " << e.path << "  (" << e.reason << ")\n";
    }
    o << "\nWarnings: " << warnings.size() << "\n";
    for (const std::string& w : warnings) o << "  " << w << "\n";
    return o.str();
}

// ---- zip -------------------------------------------------------------------------------------------

namespace {
void Put16(std::string& s, std::uint32_t v) { s += (char)(v & 0xFF); s += (char)((v >> 8) & 0xFF); }
void Put32(std::string& s, std::uint32_t v) { Put16(s, v & 0xFFFF); Put16(s, v >> 16); }
std::uint32_t Get16(const std::string& s, std::size_t i) { return (std::uint8_t)s[i] | ((std::uint32_t)(std::uint8_t)s[i + 1] << 8); }
std::uint32_t Get32(const std::string& s, std::size_t i) { return Get16(s, i) | (Get16(s, i + 2) << 16); }

bool AlreadyCompressed(const std::string& rel) {
    static const std::set<std::string> e = {"png", "jpg", "jpeg", "gif", "webp", "ogg", "mp3", "flac", "zip", "7z", "gz", "mp4", "webm", "glb", "okp"};
    return e.count(ExtOf(rel)) > 0;
}
} // namespace

bool WriteZip(const std::string& zipPath, const std::string& rootDir, bool compress, ZipStats* stats, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; std::error_code ec; fs::remove(fs::u8path(zipPath), ec); return false; };
    const fs::path root = fs::u8path(rootDir);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) { if (error) *error = "not a folder: " + rootDir; return false; }
    std::vector<std::pair<std::string, fs::path>> list;
    const fs::path zp = fs::weakly_canonical(fs::u8path(zipPath), ec);
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::error_code ce;
        if (fs::weakly_canonical(it->path(), ce) == zp) continue;   // never pack the archive into itself
        list.emplace_back(Generic(it->path().lexically_relative(root)), it->path());
    }
    std::sort(list.begin(), list.end());
    std::ofstream out(fs::u8path(zipPath), std::ios::binary | std::ios::trunc);
    if (!out) return fail("cannot write " + zipPath);

    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
    if (const std::tm* l = std::localtime(&now)) tmv = *l;
    const std::uint32_t dosTime = (std::uint32_t)((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2));
    const std::uint32_t dosDate = (std::uint32_t)((std::max(0, tmv.tm_year - 80) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday);

    struct Central { std::string name; std::uint32_t crc, csize, usize, offset; std::uint16_t method; };
    std::vector<Central> dir;
    std::uint64_t pos = 0;
    ZipStats st;
    for (const auto& f : list) {
        std::ifstream in(f.second, std::ios::binary);
        std::ostringstream buf; buf << in.rdbuf();
        const std::string data = buf.str();
        if ((std::uint64_t)data.size() >= 0xFFFFFFFFull || pos >= 0xF0000000ull) return fail("archive too large for the zip format (4 GB)");
        const std::uint8_t* d = (const std::uint8_t*)data.data();
        Central c;
        c.name = f.first;
        c.crc = ImageWrite::Crc32(d, data.size());
        c.usize = (std::uint32_t)data.size();
        c.offset = (std::uint32_t)pos;
        c.method = 0;
        std::vector<std::uint8_t> packed;
        if (compress && !data.empty() && !AlreadyCompressed(f.first)) {
            packed = ImageWrite::Deflate(d, data.size());
            if (!packed.empty() && packed.size() < data.size()) c.method = 8;
        }
        c.csize = c.method == 8 ? (std::uint32_t)packed.size() : c.usize;
        std::string h;
        Put32(h, 0x04034b50);
        Put16(h, c.method == 8 ? 20 : 10);
        Put16(h, 0x0800);                      // UTF-8 names
        Put16(h, c.method);
        Put16(h, dosTime); Put16(h, dosDate);
        Put32(h, c.crc); Put32(h, c.csize); Put32(h, c.usize);
        Put16(h, (std::uint32_t)c.name.size()); Put16(h, 0);
        out.write(h.data(), (std::streamsize)h.size());
        out.write(c.name.data(), (std::streamsize)c.name.size());
        if (c.method == 8) out.write((const char*)packed.data(), (std::streamsize)packed.size());
        else out.write(data.data(), (std::streamsize)data.size());
        pos += h.size() + c.name.size() + c.csize;
        st.files += 1; st.stored += c.usize; st.packed += c.csize;
        dir.push_back(c);
    }
    std::string cd;
    for (const Central& c : dir) {
        Put32(cd, 0x02014b50);
        Put16(cd, 20); Put16(cd, c.method == 8 ? 20 : 10);
        Put16(cd, 0x0800); Put16(cd, c.method);
        Put16(cd, dosTime); Put16(cd, dosDate);
        Put32(cd, c.crc); Put32(cd, c.csize); Put32(cd, c.usize);
        Put16(cd, (std::uint32_t)c.name.size()); Put16(cd, 0); Put16(cd, 0);
        Put16(cd, 0); Put16(cd, 0); Put32(cd, 0);
        Put32(cd, c.offset);
        cd += c.name;
    }
    std::string end;
    Put32(end, 0x06054b50);
    Put16(end, 0); Put16(end, 0);
    Put16(end, (std::uint32_t)dir.size()); Put16(end, (std::uint32_t)dir.size());
    Put32(end, (std::uint32_t)cd.size()); Put32(end, (std::uint32_t)pos);
    Put16(end, 0);
    out.write(cd.data(), (std::streamsize)cd.size());
    out.write(end.data(), (std::streamsize)end.size());
    out.close();
    if (!out) return fail("failed while writing " + zipPath);
    if (stats) *stats = st;
    return true;
}

bool ReadZipDirectory(const std::string& zipPath, std::vector<ZipEntry>& out, std::string* error) {
    std::ifstream f(fs::u8path(zipPath), std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + zipPath; return false; }
    std::ostringstream ss; ss << f.rdbuf();
    const std::string z = ss.str();
    if (z.size() < 22) { if (error) *error = "too small to be a zip"; return false; }
    std::size_t eocd = std::string::npos;
    for (std::size_t i = z.size() - 22 + 1; i-- > 0;) {
        if (Get32(z, i) == 0x06054b50) { eocd = i; break; }
        if (z.size() - i > 22 + 65535) break;
    }
    if (eocd == std::string::npos) { if (error) *error = "no end-of-directory record"; return false; }
    const std::uint32_t count = Get16(z, eocd + 10), cdSize = Get32(z, eocd + 12), cdOff = Get32(z, eocd + 16);
    if ((std::uint64_t)cdOff + cdSize > z.size()) { if (error) *error = "directory points outside the file"; return false; }
    std::size_t p = cdOff;
    out.clear();
    for (std::uint32_t k = 0; k < count; ++k) {
        if (p + 46 > z.size() || Get32(z, p) != 0x02014b50) { if (error) *error = "bad directory entry"; return false; }
        ZipEntry e;
        e.method = (int)Get16(z, p + 10);
        e.crc = Get32(z, p + 16);
        e.packed = Get32(z, p + 20);
        e.size = Get32(z, p + 24);
        const std::uint32_t nl = Get16(z, p + 28), xl = Get16(z, p + 30), cl = Get16(z, p + 32);
        if (p + 46 + nl > z.size()) { if (error) *error = "bad entry name"; return false; }
        e.name = z.substr(p + 46, nl);
        out.push_back(e);
        p += 46 + nl + xl + cl;
    }
    return true;
}

} // namespace buildpipe
} // namespace okay
