#include "ProjectBrowser.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace okaypb {

// ---- small helpers -----------------------------------------------------------------------------

namespace {

fs::path P(const std::string& s) { return fs::u8path(s); }
std::string U(const fs::path& p) { return p.generic_u8string(); }

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Windows file names ignore case; elsewhere they do not.
std::string Fold(const std::string& s) {
#ifdef _WIN32
    return Lower(s);
#else
    return s;
#endif
}

std::string StripSlash(std::string s) {
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

// Absolute-ish, lexically cleaned, '/' separated, no trailing slash.
std::string Norm(const std::string& s) {
    if (s.empty()) return s;
    fs::path p = P(Slash(s));
    std::error_code ec;
    if (p.is_relative()) {
        fs::path a = fs::absolute(p, ec);
        if (!ec) p = a;
    }
    return StripSlash(U(p.lexically_normal()));
}

bool UnderOrEqual(const std::string& parent, const std::string& child) {
    const std::string a = Fold(StripSlash(parent)), b = Fold(StripSlash(child));
    if (a == b) return true;
    return b.size() > a.size() && b.compare(0, a.size(), a) == 0 && (a.empty() || a.back() == '/' || b[a.size()] == '/');
}

std::string FileNameOf(const std::string& path) {
    const std::string s = StripSlash(Slash(path));
    const auto sl = s.find_last_of('/');
    return sl == std::string::npos ? s : s.substr(sl + 1);
}

std::string ParentOf(const std::string& path) {
    const std::string s = StripSlash(Slash(path));
    const auto sl = s.find_last_of('/');
    return sl == std::string::npos ? std::string() : (sl == 0 ? std::string("/") : s.substr(0, sl));
}

// ".png" (as written) of a file name; "" when none.
std::string ExtOf(const std::string& fileName) {
    const auto d = fileName.find_last_of('.');
    return (d == std::string::npos || d == 0) ? std::string() : fileName.substr(d);
}

bool ReadText(const std::string& path, std::string& out, std::size_t limit = 64u << 20) {
    std::ifstream in(P(path), std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    if (n < 0 || static_cast<std::size_t>(n) > limit) return false;
    in.seekg(0);
    out.assign(static_cast<std::size_t>(n), '\0');
    if (n > 0) in.read(&out[0], n);
    return static_cast<bool>(in) || in.eof();
}

bool WriteRaw(const std::string& path, const std::string& content) {
    std::ofstream out(P(path), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.flush();
    return static_cast<bool>(out);
}

bool Exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(P(path), ec));
}

} // namespace

std::string Slash(const std::string& path) {
    std::string s = path;
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

std::string ValidateName(const std::string& name) {
    std::string n = name;
    while (!n.empty() && (n.front() == ' ' || n.front() == '\t')) n.erase(n.begin());
    while (!n.empty() && (n.back() == ' ' || n.back() == '\t')) n.pop_back();
    if (n.empty()) return "Type a name.";
    if (n.size() > 200) return "That name is too long.";
    for (unsigned char c : n) {
        if (c < 32 || c == 127) return "A name cannot contain control characters.";
        if (std::string("/\\:*?\"<>|").find(static_cast<char>(c)) != std::string::npos)
            return "A name cannot contain / \\ : * ? \" < > |";
    }
    if (n == "." || n == "..") return "That name is not allowed.";
    if (n.front() == '.') return "A name cannot start with a dot (it would be hidden).";
    if (n.back() == '.') return "A name cannot end with a dot.";
    {
        std::string stem = Lower(n);
        const auto d = stem.find('.');
        if (d != std::string::npos) stem = stem.substr(0, d);
        static const char* kReserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
                                          "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
        for (const char* r : kReserved) if (stem == r) return "That name is reserved by the operating system.";
    }
    if (Lower(n).size() >= 9 && Lower(n).compare(Lower(n).size() - 9, 9, ".okaymeta") == 0) return "Names ending in .okaymeta are reserved.";
    return {};
}

bool IsInside(const std::string& root, const std::string& path) {
    if (root.empty() || path.empty()) return false;
    return UnderOrEqual(Norm(root), Norm(path));
}

std::string UniqueName(const std::string& dir, const std::string& stem, const std::string& ext, const char* sep) {
    std::string cand = stem + ext;
    for (int n = 2; Exists(U(P(dir) / P(cand))) || Exists(U(P(dir) / P(cand + ".okaymeta"))); ++n)
        cand = stem + sep + std::to_string(n) + ext;
    return cand;
}

bool NaturalLess(const std::string& a, const std::string& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            std::size_t i2 = i, j2 = j;
            while (i2 < a.size() && a[i2] == '0') ++i2;
            while (j2 < b.size() && b[j2] == '0') ++j2;
            std::size_t ie = i2, je = j2;
            while (ie < a.size() && std::isdigit(static_cast<unsigned char>(a[ie]))) ++ie;
            while (je < b.size() && std::isdigit(static_cast<unsigned char>(b[je]))) ++je;
            const std::size_t la = ie - i2, lb = je - j2;
            if (la != lb) return la < lb;
            const int c = a.compare(i2, la, b, j2, lb);
            if (c != 0) return c < 0;
            i = ie; j = je;
            continue;
        }
        const int la = std::tolower(ca), lb = std::tolower(cb);
        if (la != lb) return la < lb;
        ++i; ++j;
    }
    if (i == a.size() && j == b.size()) return false;
    return i == a.size();
}

// ---- the folder model --------------------------------------------------------------------------

bool IsHiddenName(const std::string& n) {
    if (n.empty() || n[0] == '.') return true;
    const std::string e = Lower(ExtOf(n));
    return e == ".okaymeta" || e == ".tmp" || e == ".part" || e == ".bak" || e == ".swp" || e == ".corrupt";
}

bool FilterMatches(Filter f, const std::string& e) {
    switch (f) {
        case Filter::Scripts: return e == ".okay" || e == ".lua" || e == ".cs" || e == ".okayvs";
        case Filter::Images: return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga" || e == ".gif" || e == ".hdr" || e == ".psd";
        case Filter::Scenes: return e == ".okayscene";
        case Filter::Materials: return e == ".okaymat";
        case Filter::Prefabs: return e == ".okayprefab";
        case Filter::Data: return e == ".okaydata" || e == ".okayitems" || e == ".okayloot" || e == ".okayrecipes" || e == ".json";
        case Filter::Audio: return e == ".wav" || e == ".ogg" || e == ".mp3" || e == ".flac";
        case Filter::Models: return okay::AssetTypeFromExtension(e) == okay::AssetType::Model;
        case Filter::All: case Filter::Folders: return true;
    }
    return true;
}

std::string TypeLabel(const std::string& e, bool isDir) {
    if (isDir) return "Folder";
    if (e == ".okay" || e == ".lua" || e == ".cs") return "Script";
    if (e == ".okayvs") return "Visual script";
    if (e == ".okayscene") return "Scene";
    if (e == ".okayprefab") return "Prefab";
    if (e == ".okaymat") return "Material";
    if (e == ".okaydata") return "Data";
    if (e == ".okayanim" || e == ".okayclip") return "Animation";
    if (e == ".okaydialog") return "Dialogue";
    if (e == ".okayquest") return "Quest";
    if (e == ".okaybt") return "Behavior tree";
    if (e == ".okayseq") return "Cutscene";
    using T = okay::AssetType;
    switch (okay::AssetTypeFromExtension(e)) {
        case T::Texture: return "Image";
        case T::Model: return "Model";
        case T::Audio: return "Audio";
        case T::Font: return "Font";
        case T::Mesh: return "Mesh";
        case T::Data: return "Data";
        case T::Animation: return "Animation";
        default: break;
    }
    return e.size() > 1 ? Lower(e.substr(1)) + " file" : "File";
}

std::string HumanSize(std::uint64_t z) {
    char b[48];
    if (z < 1024) std::snprintf(b, sizeof b, "%llu B", static_cast<unsigned long long>(z));
    else if (z < 1024ull * 1024) std::snprintf(b, sizeof b, "%llu KB", static_cast<unsigned long long>(z / 1024));
    else if (z < 1024ull * 1024 * 1024) std::snprintf(b, sizeof b, "%.1f MB", static_cast<double>(z) / (1024.0 * 1024.0));
    else std::snprintf(b, sizeof b, "%.1f GB", static_cast<double>(z) / (1024.0 * 1024.0 * 1024.0));
    return b;
}

Query ParseQuery(const std::string& text) {
    Query q;
    std::istringstream in(Lower(text));
    std::string w;
    while (in >> w) {
        if (w.rfind("t:", 0) == 0) {
            const std::string t = w.substr(2);
            if (t == "model" || t == "models" || t == "mesh") q.filter = Filter::Models;
            else if (t == "texture" || t == "textures" || t == "image" || t == "images" || t == "sprite") q.filter = Filter::Images;
            else if (t == "audio" || t == "sound" || t == "sounds" || t == "music") q.filter = Filter::Audio;
            else if (t == "script" || t == "scripts" || t == "code") q.filter = Filter::Scripts;
            else if (t == "scene" || t == "scenes" || t == "level") q.filter = Filter::Scenes;
            else if (t == "material" || t == "materials" || t == "mat") q.filter = Filter::Materials;
            else if (t == "prefab" || t == "prefabs") q.filter = Filter::Prefabs;
            else if (t == "data") q.filter = Filter::Data;
            else if (t == "folder" || t == "dir") { q.filter = Filter::Folders; q.foldersOnly = true; }
            else q.words.push_back(w);   // not a known type: search for the text
        } else if (w.rfind("ext:", 0) == 0) {
            q.ext = w.substr(4);
            if (!q.ext.empty() && q.ext[0] != '.') q.ext = "." + q.ext;
        } else q.words.push_back(w);
    }
    return q;
}

void SortEntries(std::vector<Entry>& v, SortKey key, bool desc) {
    std::stable_sort(v.begin(), v.end(), [&](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        auto byName = [&] { return NaturalLess(a.name, b.name); };
        int c = 0;   // <0: a first, >0: b first, before applying `desc`
        switch (key) {
            case SortKey::Name: break;
            case SortKey::Type: if (a.ext != b.ext && !a.isDir) c = a.ext < b.ext ? -1 : 1; break;
            case SortKey::Size: if (a.size != b.size && !a.isDir) c = a.size < b.size ? -1 : 1; break;
            case SortKey::Modified: if (a.modified != b.modified) c = a.modified < b.modified ? -1 : 1; break;
        }
        if (key == SortKey::Name) {
            if (a.name == b.name) return false;
            return desc ? NaturalLess(b.name, a.name) : byName();
        }
        if (c != 0) return desc ? c > 0 : c < 0;
        return byName();
    });
}

std::vector<Entry> ListFolder(const std::string& dirIn, const ListOptions& opt) {
    std::vector<Entry> out;
    const std::string dir = Norm(dirIn);
    std::error_code ec;
    if (!fs::is_directory(P(dir), ec)) return out;
    Query q = ParseQuery(opt.text);
    Filter filter = q.filter != Filter::All ? q.filter : opt.filter;
    const bool searching = !q.words.empty() || !q.ext.empty() || q.filter != Filter::All;
    const bool deep = opt.recursive || searching;

    auto consider = [&](const fs::directory_entry& e) -> bool {   // false: stop
        std::error_code e1;
        const std::string name = e.path().filename().generic_u8string();
        if (IsHiddenName(name)) return true;
        Entry en;
        en.name = name;
        en.path = U(e.path());
        en.isDir = e.is_directory(e1);
        if (e.is_symlink(e1) && en.isDir) return true;   // never follow folder links out of the project
        en.ext = en.isDir ? std::string() : Lower(ExtOf(name));
        const std::string lname = Lower(name);
        for (const std::string& w : q.words) if (lname.find(w) == std::string::npos) return true;
        if (!q.ext.empty() && en.ext != q.ext) return true;
        if (filter == Filter::Folders) { if (!en.isDir) return true; }
        else if (filter != Filter::All) {
            if (en.isDir) { if (deep) return true; }
            else if (!FilterMatches(filter, en.ext)) return true;
        }
        if (!en.isDir) {
            std::error_code e2;
            const auto sz = e.file_size(e2);
            en.size = e2 ? 0 : static_cast<std::uint64_t>(sz);
            const auto t = e.last_write_time(e2);
            en.modified = e2 ? 0 : std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
        } else {
            std::error_code e2;
            const auto t = e.last_write_time(e2);
            en.modified = e2 ? 0 : std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
        }
        out.push_back(std::move(en));
        return out.size() < opt.limit;
    };

    if (!deep) {
        for (fs::directory_iterator it(P(dir), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
            if (!consider(*it)) break;
    } else {
        for (fs::recursive_directory_iterator it(P(dir), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e1;
            if (IsHiddenName(it->path().filename().generic_u8string()) || (it->is_symlink(e1) && it->is_directory(e1))) {
                if (it->is_directory(e1)) it.disable_recursion_pending();
                continue;
            }
            if (!consider(*it)) break;
        }
    }
    SortEntries(out, opt.sort, opt.descending);
    return out;
}

// ---- references: rewrite the paths in a text asset ----------------------------------------------

namespace {

// Longest-prefix mapping of an absolute path through `map`; "" if no entry covers it.
std::string MapPath(const std::string& abs, const PathMap& map) {
    for (const auto& kv : map) {
        const std::string from = StripSlash(kv.first);
        if (UnderOrEqual(from, abs)) return StripSlash(kv.second) + abs.substr(from.size());
    }
    return {};
}

std::vector<okay::FoundRef> ExtractFor(const std::string& referrerName, const std::string& text) {
    const std::string e = Lower(ExtOf(referrerName));
    if (e == ".okayscene" || e == ".okayprefab") return okay::ExtractSceneRefs(text);
    if (e == ".okaymat") return okay::ExtractMaterialRefs(text);
    return okay::ExtractTextRefs(text);
}

} // namespace

std::string RewriteReferences(const std::string& text, const std::string& referrerOld, const std::string& referrerNew,
                              const PathMap& mapIn, const std::string& assetsRoot, int* count) {
    if (count) *count = 0;
    PathMap map;
    for (const auto& kv : mapIn) map.emplace_back(Norm(kv.first), Norm(kv.second));
    const std::string root = Norm(assetsRoot);
    const std::string project = ParentOf(root);
    const std::string oldDir = ParentOf(Norm(referrerOld)), newDir = ParentOf(Norm(referrerNew));
    struct Rep { std::size_t begin, end; std::string text; };
    std::vector<Rep> reps;
    for (const okay::FoundRef& r : ExtractFor(FileNameOf(referrerOld), text)) {
        if (r.begin == std::string::npos || r.end > text.size() || r.begin > r.end || r.path.empty()) continue;
        if (r.path.find("://") != std::string::npos || r.path.rfind("proc:", 0) == 0) continue;
        const std::string n = Slash(r.path);
        const bool abs = n[0] == '/' || (n.size() > 1 && n[1] == ':');
        struct Cand { int style; std::string path; };
        std::vector<Cand> cands;
        if (abs) cands.push_back({0, Norm(n)});
        else {
            cands.push_back({1, Norm(root + "/" + n)});
            if (!project.empty()) cands.push_back({2, Norm(project + "/" + n)});
            if (!oldDir.empty()) cands.push_back({3, Norm(oldDir + "/" + n)});
        }
        for (const Cand& c : cands) {
            const std::string mapped = MapPath(c.path, map);
            if (mapped.empty()) continue;
            if (!abs && !Exists(c.path) && !Exists(mapped)) continue;   // before the move the old place exists, after it the new one
            std::string nt;
            if (c.style == 0) nt = mapped;
            else {
                const std::string base = c.style == 1 ? root : (c.style == 2 ? project : newDir);
                nt = U(P(mapped).lexically_relative(P(base)));
                if (nt.empty()) break;
            }
            if (r.path.find('\\') != std::string::npos && r.path.find('/') == std::string::npos)
                for (char& ch : nt) if (ch == '/') ch = '\\';
            if (nt != r.path) {
                if (r.quoted) {
                    std::string esc;
                    for (char ch : nt) { if (ch == '"' || ch == '\\') esc += '\\'; esc += ch; }
                    nt = esc;
                }
                reps.push_back({r.begin, r.end, nt});
            }
            break;
        }
    }
    if (reps.empty()) return text;
    std::sort(reps.begin(), reps.end(), [](const Rep& a, const Rep& b) { return a.begin > b.begin; });
    std::string out = text;
    std::size_t lastBegin = std::string::npos;
    int n = 0;
    for (const Rep& r : reps) {
        if (lastBegin != std::string::npos && r.end > lastBegin) continue;   // overlapping spans: leave the later one
        out.replace(r.begin, r.end - r.begin, r.text);
        lastBegin = r.begin;
        ++n;
    }
    if (count) *count = n;
    return out;
}

// ---- transactions: every step has an undo, a failure rolls the whole operation back -------------

struct Tx {
    Browser& b;
    std::vector<std::function<void()>> journal;   // run in reverse on rollback
    std::string error;

    explicit Tx(Browser& br) : b(br) {}

    bool Inject(const char* op, const std::string& path, const char* what) {
        if (b.m_fail && b.m_fail(op, path)) { error = std::string("Couldn't ") + what + " (" + FileNameOf(path) + "): the disk refused."; return true; }
        return false;
    }
    bool Fail(const char* what, const std::string& path, const std::error_code& ec) {
        error = std::string("Couldn't ") + what + " \"" + FileNameOf(path) + "\": " + ec.message();
        return false;
    }

    bool Rename(const std::string& from, const std::string& to, const char* what = "rename") {
        if (Inject("rename", from, what)) return false;
        std::error_code ec;
        fs::rename(P(from), P(to), ec);
        if (ec) return Fail(what, from, ec);
        journal.push_back([from, to] { std::error_code e; fs::rename(P(to), P(from), e); });
        return true;
    }
    // Case-only changes ("a.png" -> "A.png") go through a temp name so case-insensitive disks accept them.
    bool RenameItem(const std::string& from, const std::string& to) {
        if (Fold(Lower(from)) == Fold(Lower(to)) && from != to) {
            const std::string tmp = from + ".case-tmp";
            if (!Rename(from, tmp)) return false;
            return Rename(tmp, to);
        }
        return Rename(from, to);
    }
    bool Mkdir(const std::string& dir) {
        if (Inject("mkdir", dir, "create the folder")) return false;
        std::error_code ec;
        if (!fs::create_directory(P(dir), ec) || ec) { if (!ec) ec = std::make_error_code(std::errc::file_exists); return Fail("create the folder", dir, ec); }
        journal.push_back([dir] { std::error_code e; fs::remove(P(dir), e); });
        return true;
    }
    // Atomic write: temp file beside it, then rename. Restores the old content on rollback.
    bool Write(const std::string& path, const std::string& content) {
        std::string old; const bool had = Exists(path) && ReadText(path, old);
        const std::string tmp = path + ".tmp";
        if (Inject("write", path, "save")) return false;
        if (!WriteRaw(tmp, content)) { std::error_code e; fs::remove(P(tmp), e); return Fail("save", path, std::make_error_code(std::errc::io_error)); }
        if (Inject("rename", path, "save")) { std::error_code e; fs::remove(P(tmp), e); return false; }
        std::error_code ec;
        fs::rename(P(tmp), P(path), ec);
        if (ec) { std::error_code e; fs::remove(P(tmp), e); return Fail("save", path, ec); }
        if (had) journal.push_back([path, old] { WriteRaw(path, old); });
        else journal.push_back([path] { std::error_code e; fs::remove(P(path), e); });
        return true;
    }
    bool CopyFileTo(const std::string& from, const std::string& to) {
        if (Inject("copy", from, "copy")) return false;
        std::error_code ec;
        fs::copy_file(P(from), P(to), fs::copy_options::none, ec);
        if (ec) return Fail("copy", from, ec);
        journal.push_back([to] { std::error_code e; fs::remove(P(to), e); });
        return true;
    }
    bool CopyTree(const std::string& from, const std::string& to) {
        if (!Mkdir(to)) return false;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(P(from), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e1;
            if (it->is_symlink(e1)) { if (it->is_directory(e1)) it.disable_recursion_pending(); continue; }
            const fs::path rel = it->path().lexically_relative(P(from));
            const std::string dst = U(P(to) / rel);
            if (it->is_directory(e1)) { if (!Mkdir(dst)) return false; }
            else if (!CopyFileTo(U(it->path()), dst)) return false;
        }
        if (ec) return Fail("read the folder", from, ec);
        return true;
    }
    // Used on rollback, so no injection.
    void Rollback() {
        for (auto it = journal.rbegin(); it != journal.rend(); ++it) (*it)();
        journal.clear();
    }
};

// ---- Browser -------------------------------------------------------------------------------------

Browser::Browser(std::string assetsRoot) : m_root(Norm(assetsRoot)) {}
Browser::~Browser() = default;

std::string Browser::ProjectDir() const { return ParentOf(m_root); }

bool Browser::Sync() {
    std::error_code ec;
    if (!fs::is_directory(P(m_root), ec)) return false;
    okay::ScanOptions o;
    o.logWarnings = false;
    o.fullHashLimit = 32u << 20;   // big files are hashed from their head, tail and size
    m_db.Scan(m_root, o);
    m_indexed = true;
    return true;
}

bool Browser::Poll(std::uint64_t nowMs) {
    if (!m_indexed || nowMs < m_nextPoll) return false;
    const auto t0 = std::chrono::steady_clock::now();
    std::error_code ec;
    if (!fs::is_directory(P(m_root), ec)) { m_nextPoll = nowMs + 2000; return false; }
    okay::ScanOptions o;
    o.logWarnings = false;
    o.fullHashLimit = 32u << 20;
    const okay::ScanReport rep = m_db.Scan(m_root, o);
    const auto ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    m_nextPoll = nowMs + std::max<std::uint64_t>(2000, 30 * ms);
    return rep.created > 0 || !rep.moved.empty() || !rep.removed.empty() || !rep.reimports.empty();
}

int Browser::UsersOf(const std::string& path) {
    if (!m_indexed && !Sync()) return 0;
    const okay::Guid g = m_db.PathToGuid(m_db.ToProjectPath(Norm(path)));
    return g ? static_cast<int>(m_db.UsedBy(g).size()) : 0;
}

void Browser::PushUndo(const std::string& label, std::function<Result()> run) {
    m_undo.push_back({label, std::move(run)});
    if (m_undo.size() > 50) m_undo.erase(m_undo.begin());
}

Result Browser::Undo() {
    Result r;
    if (m_undo.empty()) { r.error = "Nothing to undo."; return r; }
    UndoEntry e = std::move(m_undo.back());
    m_undo.pop_back();
    r = e.run();
    if (!r.ok) {
        // Keep the entry so the user can try again after fixing the problem.
        m_undo.push_back(std::move(e));
    }
    return r;
}

std::string Browser::CheckInside(const std::string& path, bool allowRoot) const {
    if (path.empty()) return "No path was given.";
    const std::string n = Norm(path);
    if (!UnderOrEqual(m_root, n)) return "That is outside the project's Assets folder.";
    if (!allowRoot && Fold(n) == Fold(m_root)) return "The Assets folder itself cannot be changed.";
    // A link inside the project that points outside it is not allowed either.
    std::error_code ec;
    const fs::path real = fs::weakly_canonical(P(n), ec);
    const fs::path rroot = fs::weakly_canonical(P(m_root), ec);
    if (!ec && !UnderOrEqual(U(rroot), U(real))) return "That is outside the project's Assets folder.";
    return {};
}

Result Browser::CreateFolder(const std::string& dir, const std::string& name) {
    Result r;
    if (std::string e = ValidateName(name); !e.empty()) { r.error = e; return r; }
    if (std::string e = CheckInside(dir, true); !e.empty()) { r.error = e; return r; }
    std::string nm = name;
    while (!nm.empty() && nm.front() == ' ') nm.erase(nm.begin());
    while (!nm.empty() && nm.back() == ' ') nm.pop_back();
    const std::string path = Norm(dir) + "/" + nm;
    if (Exists(path)) { r.error = "\"" + nm + "\" already exists here."; return r; }
    Tx tx(*this);
    if (!tx.Mkdir(path)) { r.error = tx.error; return r; }
    r.ok = true;
    r.paths.push_back(path);
    PushUndo("Create folder", [this, path]() { return TrashCore({path}, false); });
    return r;
}

Result Browser::CreateFile(const std::string& dir, const std::string& fileName, const std::string& content) {
    Result r;
    if (std::string e = ValidateName(fileName); !e.empty()) { r.error = e; return r; }
    if (std::string e = CheckInside(dir, true); !e.empty()) { r.error = e; return r; }
    std::string nm = fileName;
    while (!nm.empty() && nm.front() == ' ') nm.erase(nm.begin());
    while (!nm.empty() && nm.back() == ' ') nm.pop_back();
    const std::string path = Norm(dir) + "/" + nm;
    if (Exists(path)) { r.error = "\"" + nm + "\" already exists here."; return r; }
    Tx tx(*this);
    if (!tx.Write(path, content)) { r.error = tx.error; return r; }
    r.ok = true;
    r.paths.push_back(path);
    PushUndo("Create " + nm, [this, path]() { return TrashCore({path}, false); });
    return r;
}

namespace {
// Keep only the outermost paths (a folder and a file inside it: just the folder), dropping duplicates.
std::vector<std::string> Outermost(const std::vector<std::string>& in) {
    std::vector<std::string> v;
    for (const std::string& p : in) v.push_back(Norm(p));
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    std::vector<std::string> out;
    for (const std::string& p : v) {
        bool nested = false;
        for (const std::string& o : v) if (o != p && UnderOrEqual(o, p)) { nested = true; break; }
        if (!nested) out.push_back(p);
    }
    return out;
}
} // namespace

Result Browser::MoveCore(const std::vector<std::pair<std::string, std::string>>& items, const std::string& label, bool pushUndo) {
    Result r;
    if (items.empty()) { r.ok = true; return r; }
    std::set<std::string> dests;
    PathMap map;
    for (const auto& it : items) {
        const std::string from = Norm(it.first), to = Norm(it.second);
        if (std::string e = CheckInside(from, false); !e.empty()) { r.error = e; return r; }
        if (std::string e = CheckInside(to, false); !e.empty()) { r.error = e; return r; }
        if (!Exists(from)) { r.error = "\"" + FileNameOf(from) + "\" is not there any more."; return r; }
        std::error_code ec;
        if (fs::is_directory(P(from), ec) && UnderOrEqual(from, to)) { r.error = "A folder cannot be moved into itself."; return r; }
        const bool caseOnly = Lower(from) == Lower(to) && from != to;
        if (Fold(from) != Fold(to) || caseOnly) {
            if (!caseOnly && (Exists(to) || Exists(to + ".okaymeta"))) { r.error = "\"" + FileNameOf(to) + "\" already exists in that folder."; return r; }
        }
        if (!dests.insert(Fold(to)).second) { r.error = "Two items would get the same name."; return r; }
        if (from != to) map.emplace_back(from, to);
    }
    if (map.empty()) { r.ok = true; for (const auto& it : items) r.paths.push_back(Norm(it.second)); return r; }
    if (!Sync()) { r.error = "The Assets folder could not be read."; return r; }

    // Who uses the things that move: gather before anything changes.
    std::set<std::string> referrers;
    for (const auto& kv : map) {
        const std::string rel = m_db.ToProjectPath(kv.first);
        if (rel.empty()) continue;
        std::vector<okay::Guid> guids;
        std::error_code ec;
        if (fs::is_directory(P(kv.first), ec)) {
            for (const okay::AssetRecord* rec : m_db.All())
                if (rec->path.size() > rel.size() && rec->path.compare(0, rel.size(), rel) == 0 && rec->path[rel.size()] == '/') guids.push_back(rec->guid);
        } else if (okay::Guid g = m_db.PathToGuid(rel)) guids.push_back(g);
        for (const okay::Guid& g : guids)
            for (const okay::Guid& u : m_db.UsedBy(g)) {
                const std::string ap = m_db.AbsolutePath(u);
                if (!ap.empty()) referrers.insert(Norm(ap));
            }
    }
    struct Staged { std::string newPath, text; int n; };
    std::vector<Staged> staged;
    std::string skipped;
    for (const std::string& ref : referrers) {
        std::string text;
        if (!ReadText(ref, text, 32u << 20)) { skipped += (skipped.empty() ? "" : ", ") + FileNameOf(ref); continue; }
        std::string newRef = MapPath(ref, map);
        if (newRef.empty()) newRef = ref;
        int n = 0;
        std::string out = RewriteReferences(text, ref, newRef, map, m_root, &n);
        if (n > 0 && out != text) staged.push_back({newRef, std::move(out), n});
    }

    Tx tx(*this);
    auto fail = [&](const std::string& why) {
        tx.Rollback();
        r.error = why + " Nothing was changed.";
        Sync();
        return r;
    };
    for (const auto& kv : map) {
        std::error_code ec;
        const bool isDir = fs::is_directory(P(kv.first), ec);
        if (!tx.RenameItem(kv.first, kv.second)) return fail(tx.error);
        if (!isDir && Exists(kv.first + ".okaymeta")) {
            if (!tx.RenameItem(kv.first + ".okaymeta", kv.second + ".okaymeta")) return fail(tx.error);
        }
    }
    for (const Staged& s : staged)
        if (!tx.Write(s.newPath, s.text)) return fail(tx.error);

    Sync();
    r.ok = true;
    r.moved = map;
    for (const auto& it : items) r.paths.push_back(Norm(it.second));
    for (const Staged& s : staged) { r.referencesRewritten += s.n; ++r.filesRewritten; }
    if (!skipped.empty()) r.note = "These files use the moved item but could not be read, so they were not updated: " + skipped + ".";
    if (pushUndo) {
        std::vector<std::pair<std::string, std::string>> back;
        for (const auto& kv : map) back.emplace_back(kv.second, kv.first);
        PushUndo(label, [this, back, label]() { return MoveCore(back, label, false); });
    }
    return r;
}

Result Browser::Rename(const std::string& path, const std::string& newName) {
    Result r;
    if (std::string e = CheckInside(path, false); !e.empty()) { r.error = e; return r; }
    const std::string from = Norm(path);
    std::string nm = newName;
    while (!nm.empty() && (nm.front() == ' ' || nm.front() == '\t')) nm.erase(nm.begin());
    while (!nm.empty() && (nm.back() == ' ' || nm.back() == '\t')) nm.pop_back();
    if (std::string e = ValidateName(nm); !e.empty()) { r.error = e; return r; }
    std::error_code ec;
    const bool isDir = fs::is_directory(P(from), ec);
    if (!Exists(from)) { r.error = "\"" + FileNameOf(from) + "\" is not there any more."; return r; }
    if (!isDir) {
        const std::string oldExt = ExtOf(FileNameOf(from));
        if (!oldExt.empty() && (nm.size() < oldExt.size() || Lower(nm.substr(nm.size() - oldExt.size())) != Lower(oldExt))) nm += oldExt;
    }
    const std::string to = ParentOf(from) + "/" + nm;
    if (to == from) { r.ok = true; r.paths.push_back(from); return r; }
    return MoveCore({{from, to}}, "Rename " + FileNameOf(from), true);
}

Result Browser::Move(const std::vector<std::string>& pathsIn, const std::string& destDir) {
    Result r;
    if (std::string e = CheckInside(destDir, true); !e.empty()) { r.error = e; return r; }
    std::error_code ec;
    if (!fs::is_directory(P(destDir), ec)) { r.error = "The destination is not a folder."; return r; }
    const std::string dest = Norm(destDir);
    std::vector<std::pair<std::string, std::string>> items;
    std::set<std::string> taken;
    for (const std::string& from : Outermost(pathsIn)) {
        if (std::string e = CheckInside(from, false); !e.empty()) { r.error = e; return r; }
        const std::string name = FileNameOf(from);
        if (Fold(ParentOf(from)) == Fold(dest)) continue;   // already there
        std::string stem = name, ext;
        if (!fs::is_directory(P(from), ec)) { ext = ExtOf(name); stem = name.substr(0, name.size() - ext.size()); }
        std::string cand = name;
        for (int n = 2; Exists(dest + "/" + cand) || Exists(dest + "/" + cand + ".okaymeta") || taken.count(Fold(cand)); ++n) cand = stem + " " + std::to_string(n) + ext;
        taken.insert(Fold(cand));
        items.emplace_back(from, dest + "/" + cand);
    }
    return MoveCore(items, "Move", true);
}

Result Browser::CopyCore(const std::vector<std::string>& sources, const std::vector<std::string>& dests, const std::string& label) {
    Result r;
    Tx tx(*this);
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const std::string &from = sources[i], &to = dests[i];
        std::error_code ec;
        const bool isDir = fs::is_directory(P(from), ec);
        if (isDir && UnderOrEqual(from, to)) { tx.Rollback(); r.error = "A folder cannot be copied into itself."; return r; }
        const std::string tmp = to + ".tmp";
        bool ok = isDir ? tx.CopyTree(from, tmp) : tx.CopyFileTo(from, tmp);
        if (ok) ok = tx.Rename(tmp, to, "copy");
        if (ok && !isDir && Exists(from + ".okaymeta")) {
            // The copy brings its labels and import settings; the database gives it a new GUID.
            const std::string mtmp = to + ".okaymeta.tmp";
            ok = tx.CopyFileTo(from + ".okaymeta", mtmp) && tx.Rename(mtmp, to + ".okaymeta", "copy");
        }
        if (!ok) { tx.Rollback(); r.error = tx.error + " Nothing was copied."; return r; }
        r.paths.push_back(to);
    }
    Sync();
    r.ok = true;
    std::vector<std::string> made = r.paths;
    PushUndo(label, [this, made]() { return TrashCore(made, false); });
    return r;
}

Result Browser::Copy(const std::vector<std::string>& pathsIn, const std::string& destDir) {
    Result r;
    if (std::string e = CheckInside(destDir, true); !e.empty()) { r.error = e; return r; }
    std::error_code ec;
    if (!fs::is_directory(P(destDir), ec)) { r.error = "The destination is not a folder."; return r; }
    const std::string dest = Norm(destDir);
    std::vector<std::string> src, dst;
    std::set<std::string> taken;
    for (const std::string& from : Outermost(pathsIn)) {
        if (std::string e = CheckInside(from, false); !e.empty()) { r.error = e; return r; }
        if (!Exists(from)) { r.error = "\"" + FileNameOf(from) + "\" is not there any more."; return r; }
        const std::string name = FileNameOf(from);
        std::string stem = name, ext;
        if (!fs::is_directory(P(from), ec)) { ext = ExtOf(name); stem = name.substr(0, name.size() - ext.size()); }
        std::string cand = name;
        for (int n = 2; Exists(dest + "/" + cand) || Exists(dest + "/" + cand + ".okaymeta") || taken.count(Fold(cand)); ++n) cand = stem + " " + std::to_string(n) + ext;
        taken.insert(Fold(cand));
        src.push_back(from);
        dst.push_back(dest + "/" + cand);
    }
    if (src.empty()) { r.ok = true; return r; }
    return CopyCore(src, dst, "Copy");
}

Result Browser::Duplicate(const std::string& path) {
    Result r;
    if (std::string e = CheckInside(path, false); !e.empty()) { r.error = e; return r; }
    const std::string from = Norm(path);
    if (!Exists(from)) { r.error = "\"" + FileNameOf(from) + "\" is not there any more."; return r; }
    std::error_code ec;
    const std::string name = FileNameOf(from);
    std::string stem = name, ext;
    if (!fs::is_directory(P(from), ec)) { ext = ExtOf(name); stem = name.substr(0, name.size() - ext.size()); }
    const std::string dir = ParentOf(from);
    const std::string cand = UniqueName(dir, stem + " copy", ext, " ");
    return CopyCore({from}, {dir + "/" + cand}, "Duplicate " + name);
}

namespace {
std::string TrashDirOf(const std::string& projectDir) { return projectDir + "/.okay/trash"; }

std::vector<int> TrashIds(const std::string& trash) {
    std::vector<int> ids;
    std::error_code ec;
    for (fs::directory_iterator it(P(trash), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().generic_u8string();
        if (!n.empty() && std::all_of(n.begin(), n.end(), [](unsigned char c) { return std::isdigit(c); })) ids.push_back(std::atoi(n.c_str()));
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}
std::string IdName(int id) { char b[16]; std::snprintf(b, sizeof b, "%06d", id); return b; }
} // namespace

Result Browser::TrashCore(const std::vector<std::string>& pathsIn, bool pushUndo) {
    Result r;
    const std::vector<std::string> paths = Outermost(pathsIn);
    if (paths.empty()) { r.ok = true; return r; }
    for (const std::string& p : paths) {
        if (std::string e = CheckInside(p, false); !e.empty()) { r.error = e; return r; }
        if (!Exists(p)) { r.error = "\"" + FileNameOf(p) + "\" is not there any more."; return r; }
    }
    Sync();   // so the meta files and the database follow the move
    const std::string trash = TrashDirOf(ProjectDir());
    std::error_code ec;
    fs::create_directories(P(trash), ec);
    const std::vector<int> ids = TrashIds(trash);
    const int id = ids.empty() ? 1 : ids.back() + 1;
    const std::string idDir = trash + "/" + IdName(id);
    Tx tx(*this);
    auto fail = [&]() {
        tx.Rollback();
        std::error_code e4;
        fs::remove(P(trash), e4);                       // (only if empty: made just for this)
        fs::remove(P(ProjectDir() + "/.okay"), e4);
        r.error = tx.error + " Nothing was deleted.";
        return r;
    };
    if (!tx.Mkdir(idDir)) return fail();
    std::string manifest;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const std::string slot = idDir + "/" + std::to_string(i);
        if (!tx.Rename(paths[i], slot, "delete")) return fail();
        std::error_code e2;
        if (!fs::is_directory(P(slot), e2) && Exists(paths[i] + ".okaymeta")) {
            if (!tx.Rename(paths[i] + ".okaymeta", slot + ".okaymeta", "delete")) return fail();
        }
        manifest += std::to_string(i) + "\t" + paths[i] + "\n";
    }
    if (!tx.Write(idDir + "/manifest.txt", manifest)) return fail();
    // Keep the trash small: the newest 30 deletions stay.
    {
        std::vector<int> all = TrashIds(trash);
        while (all.size() > 30) {
            std::error_code e3;
            fs::remove_all(P(trash + "/" + IdName(all.front())), e3);
            all.erase(all.begin());
        }
    }
    Sync();
    r.ok = true;
    r.paths = paths;
    const std::string label = paths.size() == 1 ? "Delete " + FileNameOf(paths[0]) : "Delete " + std::to_string(paths.size()) + " items";
    if (pushUndo) PushUndo(label, [this, idDir, paths]() {
        Result u;
        std::string text;
        if (!ReadText(idDir + "/manifest.txt", text)) { u.error = "The deleted items can't be found in the trash."; return u; }
        std::vector<std::pair<std::string, std::string>> back;   // slot -> original
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            const auto tab = line.find('\t');
            if (tab == std::string::npos) continue;
            back.emplace_back(idDir + "/" + line.substr(0, tab), line.substr(tab + 1));
        }
        for (const auto& b : back)
            if (Exists(b.second) || Exists(b.second + ".okaymeta")) { u.error = "\"" + FileNameOf(b.second) + "\" exists again, so it can't be restored."; return u; }
        Tx tx2(*this);
        for (const auto& b : back) {
            std::error_code e;
            fs::create_directories(P(ParentOf(b.second)), e);
            if (!tx2.Rename(b.first, b.second, "restore")) { tx2.Rollback(); u.error = tx2.error + " Nothing was restored."; return u; }
            if (Exists(b.first + ".okaymeta") && !tx2.Rename(b.first + ".okaymeta", b.second + ".okaymeta", "restore")) { tx2.Rollback(); u.error = tx2.error + " Nothing was restored."; return u; }
            u.paths.push_back(b.second);
        }
        std::error_code e;
        fs::remove_all(P(idDir), e);
        Sync();
        u.ok = true;
        return u;
    });
    return r;
}

Result Browser::Trash(const std::vector<std::string>& paths) { return TrashCore(paths, true); }

int Browser::TrashCount() const { return static_cast<int>(TrashIds(TrashDirOf(ProjectDir())).size()); }

bool Browser::EmptyTrash() {
    std::error_code ec;
    const std::string trash = TrashDirOf(ProjectDir());
    bool ok = true;
    for (int id : TrashIds(trash)) {
        if (m_fail && m_fail("remove", trash + "/" + IdName(id))) { ok = false; continue; }
        fs::remove_all(P(trash + "/" + IdName(id)), ec);
        if (ec) ok = false;
    }
    // Entries in the undo list that pointed into the trash are gone now.
    m_undo.erase(std::remove_if(m_undo.begin(), m_undo.end(), [](const UndoEntry& e) { return e.label.rfind("Delete", 0) == 0; }), m_undo.end());
    return ok;
}

std::vector<Referrer> Browser::ReferencesTo(const std::vector<std::string>& pathsIn) {
    std::vector<Referrer> out;
    if (!Sync()) return out;
    const std::vector<std::string> paths = Outermost(pathsIn);
    std::set<std::pair<std::string, std::string>> seen;
    for (const std::string& p : paths) {
        const std::string rel = m_db.ToProjectPath(p);
        if (rel.empty()) continue;
        std::vector<const okay::AssetRecord*> targets;
        std::error_code ec;
        if (fs::is_directory(P(p), ec)) {
            for (const okay::AssetRecord* rec : m_db.All())
                if (rec->path.size() > rel.size() && rec->path.compare(0, rel.size(), rel) == 0 && rec->path[rel.size()] == '/') targets.push_back(rec);
        } else if (const okay::AssetRecord* rec = m_db.Find(rel)) targets.push_back(rec);
        for (const okay::AssetRecord* t : targets)
            for (const okay::Guid& u : m_db.UsedBy(t->guid)) {
                const std::string ap = Norm(m_db.AbsolutePath(u));
                if (ap.empty()) continue;
                bool inside = false;
                for (const std::string& q : paths) if (UnderOrEqual(q, ap)) { inside = true; break; }
                if (inside) continue;
                const std::string tp = Norm(m_db.AbsolutePath(t->guid));
                if (seen.insert({ap, tp}).second) out.push_back({ap, tp});
            }
    }
    std::sort(out.begin(), out.end(), [](const Referrer& a, const Referrer& b) { return a.path != b.path ? a.path < b.path : a.target < b.target; });
    return out;
}

std::vector<std::string> Browser::DependenciesOf(const std::string& path) {
    std::vector<std::string> out;
    if (!Sync()) return out;
    const okay::Guid g = m_db.PathToGuid(m_db.ToProjectPath(Norm(path)));
    if (!g) return out;
    for (const okay::Guid& d : m_db.DependenciesOf(g)) {
        const std::string ap = m_db.AbsolutePath(d);
        if (!ap.empty()) out.push_back(Norm(ap));
    }
    return out;
}

// ---- clipboard -----------------------------------------------------------------------------------

Result Clipboard::Paste(Browser& browser, const std::string& destDir) {
    Result r;
    std::vector<std::string> live;
    std::string missing;
    for (const std::string& p : items) {
        if (Exists(p)) live.push_back(p);
        else missing += (missing.empty() ? "" : ", ") + FileNameOf(p);
    }
    if (live.empty()) { r.error = items.empty() ? "Nothing to paste." : "The copied items are gone."; return r; }
    r = cut ? browser.Move(live, destDir) : browser.Copy(live, destDir);
    if (!missing.empty()) r.note += (r.note.empty() ? "" : " ") + std::string("Skipped (not there any more): ") + missing + ".";
    if (r.ok && cut) Clear();
    return r;
}

} // namespace okaypb
