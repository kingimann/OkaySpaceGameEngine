#pragma once
// Making a new project: the rules the editor's New Project dialog, the launcher's
// Create page and the tests share. Header-only and free of engine and UI code, so the
// launcher (which does not link the engine) can use it too.
//
//   CheckName      "My Game" -> the folder name, or why the name can't be used
//   CheckLocation  the folder the project goes in: exists or can be made, can be written, has room
//   PlanProject    name + location -> the full path the project would get (Name, Name-2, ...)
//   MakeSkeleton   Assets and the standard folders; undoes itself if anything fails
//   WriteTextAtomic  a file is replaced whole or not at all, and a failure says why
//
// Nothing here prints or throws: every function returns text for the UI to show.
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace okayproj {
namespace fs = std::filesystem;

/// The folders a new project starts with inside Assets (the Project panel's "Create >
/// Standard folders" makes the same set).
inline const std::vector<std::string>& StandardFolders() {
    static const std::vector<std::string> f = {"Scenes", "Scripts", "Prefabs", "Materials", "Models", "Textures", "Audio"};
    return f;
}

/// The project settings file, in the project folder.
inline const char* SettingsFileName() { return "project.okayproj"; }

constexpr std::size_t kMaxNameChars = 64;
/// The longest project folder path accepted. Windows stops at 260 characters, and the editor
/// writes files about this much deeper (Assets/<Name>.okayscene.autosave.savetmp), so a folder
/// longer than this could never be saved into.
constexpr std::size_t kMaxRootPath = 199;
constexpr std::uintmax_t kMinFreeBytes = 16u * 1024u * 1024u;

// ---- the name -----------------------------------------------------------------------------------------------

struct NameCheck {
    std::string folder;    ///< what the folder will be called (the name, trimmed)
    std::string problem;   ///< empty when the name is fine; otherwise one sentence for the UI
    bool ok() const { return problem.empty(); }
};

inline std::size_t Utf8Length(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

inline bool IsReservedDeviceName(const std::string& name) {
    std::string stem = name.substr(0, name.find('.'));
    for (char& c : stem) c = (char)std::toupper((unsigned char)c);
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return true;
    if (stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) && stem[3] >= '1' && stem[3] <= '9') return true;
    return false;
}

/// Check what the person typed in the Name box. The result's `folder` is the name with the spaces
/// at both ends removed; `problem` is empty when it can be used as a folder name on every platform.
inline NameCheck CheckName(const std::string& raw) {
    NameCheck r;
    std::size_t b = 0, e = raw.size();
    while (b < e && (raw[b] == ' ' || raw[b] == '\t')) ++b;
    while (e > b && (raw[e - 1] == ' ' || raw[e - 1] == '\t')) --e;
    r.folder = raw.substr(b, e - b);
    if (r.folder.empty()) { r.problem = "Give the project a name."; return r; }
    for (unsigned char c : r.folder) {
        if (c < 0x20 || c == 0x7F) { r.problem = "A name can't contain control characters."; return r; }
        if (std::strchr("/\\:*?\"<>|", c)) {
            r.problem = std::string("A name can't contain \"") + (char)c + "\". These are not allowed: / \\ : * ? \" < > |";
            return r;
        }
    }
    if (r.folder == "." || r.folder == "..") { r.problem = "That name isn't allowed."; return r; }
    if (r.folder[0] == '.') { r.problem = "A name can't start with a dot (the folder would be hidden)."; return r; }
    if (r.folder.back() == '.') { r.problem = "A name can't end with a dot."; return r; }
    if (IsReservedDeviceName(r.folder)) { r.problem = "\"" + r.folder + "\" is a reserved name on Windows. Pick another."; return r; }
    if (Utf8Length(r.folder) > kMaxNameChars) {
        r.problem = "That name is too long (at most " + std::to_string(kMaxNameChars) + " characters).";
        return r;
    }
    return r;
}

/// Turn any text into a name CheckName accepts: illegal characters become '-', the ends are
/// trimmed, a reserved or empty result falls back to "MyGame". For callers (the launcher) that
/// silently fix a name instead of refusing it.
inline std::string SanitizeName(const std::string& raw) {
    std::string s;
    for (unsigned char c : raw) s += (c < 0x20 || c == 0x7F || std::strchr("/\\:*?\"<>|", c)) ? '-' : (char)c;
    while (!s.empty() && (s.back() == ' ' || s.back() == '.' || s.back() == '\t')) s.pop_back();
    std::size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '.')) ++b;
    s = s.substr(b);
    while (Utf8Length(s) > kMaxNameChars) {   // cut whole characters off the end
        std::size_t cut = s.size() - 1;
        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;
        s.erase(cut);
    }
    if (s.empty() || IsReservedDeviceName(s)) s = "MyGame";
    return s;
}

// ---- the templates ------------------------------------------------------------------------------------------

/// One starting scene a new project can begin from. `title` is what the launcher passes as --template and
/// what the editor looks up; `group` indexes TemplateGroups().
struct TemplateInfo { const char* title; const char* blurb; int group; };

inline const std::vector<TemplateInfo>& Templates() {
    static const std::vector<TemplateInfo> t = {
        {"3D Scene",             "Ground, light and sky. Start here for 3D.", 1},
        {"2D Scene",             "A blank 2D canvas with a camera.", 1},
        {"Empty",                "Nothing at all: build it your way.", 1},
        {"First Person",         "Mouse-look, WASD and jump.", 2},
        {"Third Person",         "Orbit camera and a character controller.", 2},
        {"Third Person Shooter", "Over-the-shoulder aiming and shooting.", 2},
        {"Platformer",           "Side-scrolling jump-and-run.", 2},
        {"Top-Down",             "Top-down movement and rooms.", 2},
        {"Top-Down Shooter",     "Twin-stick 3D shooter: mouse aim, dash, ammo.", 2},
        {"2.5D Platformer",      "Side view with depth layers and grabbing.", 2},
        {"Point & Click",        "Click-to-move adventure starter.", 2},
        {"Vehicle (3D)",         "A drivable car with suspension.", 2},
        {"Vehicle (2D)",         "A side-on car for hill-climb games.", 2},
        {"Sandbox",              "Place and break blocks in a voxel world.", 2},
        {"Multiplayer",          "Host / join networked starter.", 3},
        {"Coin Collector",       "A complete pickup-the-coins game.", 3},
        {"Snake",                "The classic, fully playable.", 3},
        {"Brick Breaker",        "Paddle, ball, and a wall of bricks.", 3},
        {"Main Menu",            "A title screen with buttons.", 3},
        {"Inventory",            "Drag-and-drop item grid.", 3},
        {"Humanoid",             "A rigged character to animate.", 3},
    };
    return t;
}
/// Group names; index 0 is "All".
inline const std::vector<std::string>& TemplateGroups() {
    static const std::vector<std::string> g = {"All", "Start simple", "Game kits", "Examples & systems"};
    return g;
}
/// The template called `title`, or null.
inline const TemplateInfo* FindTemplate(const std::string& title) {
    for (const TemplateInfo& t : Templates()) if (title == t.title) return &t;
    return nullptr;
}

// ---- the location -------------------------------------------------------------------------------------------

inline fs::path Absolute(const fs::path& p) {
    std::error_code ec;
    fs::path a = fs::absolute(p, ec);
    return ec ? p : a.lexically_normal();
}

/// The nearest folder at or above `p` that exists ("" when none does).
inline fs::path NearestExisting(fs::path p) {
    std::error_code ec;
    p = Absolute(p);
    while (!p.empty() && !fs::exists(p, ec)) {
        fs::path up = p.parent_path();
        if (up == p) return {};
        p = up;
    }
    return p;
}

/// True when a file can be made in `dir` (tries it; permission bits alone lie on network
/// drives and for administrators).
inline bool CanWriteIn(const fs::path& dir, std::string* why = nullptr) {
    std::error_code ec;
    const fs::path probe = dir / (".okay_write_test_" + std::to_string((unsigned long long)std::hash<std::string>()(dir.string()) % 100000u));
    {
        std::ofstream f(probe, std::ios::binary | std::ios::trunc);
        if (!f) { if (why) *why = std::error_code(errno, std::generic_category()).message(); return false; }
        f << "x";
        f.flush();
        if (!f) { if (why) *why = std::error_code(errno, std::generic_category()).message(); f.close(); fs::remove(probe, ec); return false; }
    }
    fs::remove(probe, ec);
    return true;
}

/// Why `location` can't hold a new project, or "" when it can (it may not exist yet: it is made).
inline std::string CheckLocation(const fs::path& location, std::uintmax_t minFree = kMinFreeBytes) {
    if (location.empty()) return "Choose where the project goes.";
    std::error_code ec;
    const fs::path abs = Absolute(location);
    if (fs::exists(abs, ec)) {
        if (!fs::is_directory(abs, ec)) return "That location is a file, not a folder.";
    } else {
        const fs::path up = NearestExisting(abs);
        if (up.empty()) return "That location can't be reached (is the drive connected?).";
        if (!fs::is_directory(up, ec)) return "\"" + up.string() + "\" is a file, so a folder can't be made inside it.";
    }
    const fs::path where = fs::exists(abs, ec) ? abs : NearestExisting(abs);
    std::string why;
    if (!CanWriteIn(where, &why))
        return "Can't write to \"" + where.string() + "\" (" + (why.empty() ? std::string("read-only or no permission") : why) + "). Pick another location.";
    const fs::space_info sp = fs::space(where, ec);
    if (!ec && sp.available != static_cast<std::uintmax_t>(-1) && sp.available < minFree)
        return "There is almost no free space on that drive (" + std::to_string(sp.available / (1024 * 1024)) + " MB). Free some up or pick another location.";
    return "";
}

// ---- the plan -----------------------------------------------------------------------------------------------

/// A folder counts as used when it holds a project that has files, or clutter that isn't a project.
/// An empty folder, or one that is only an empty Assets (just made by the launcher), is free.
inline bool LooksUsed(const fs::path& root) {
    std::error_code ec;
    if (!fs::exists(root, ec)) return false;
    if (!fs::is_directory(root, ec)) return true;
    const fs::path assets = root / "Assets";
    if (!fs::exists(assets, ec)) return !fs::is_empty(root, ec);
    if (!fs::is_empty(assets, ec)) return true;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))   // Assets is empty: anything else beside it?
        if (it->path().filename() != "Assets") return true;
    return false;
}

struct Plan {
    fs::path location;           ///< where it goes (absolute)
    std::string name;            ///< the name as asked
    std::string folder;          ///< the folder name actually used (Name, or Name-2 when Name is taken)
    fs::path root;               ///< location / folder
    bool renamed = false;        ///< folder differs from the name because the name was taken
    std::string nameProblem, locationProblem, pathProblem;
    bool ok() const { return nameProblem.empty() && locationProblem.empty() && pathProblem.empty(); }
    /// The one problem to show first ("" when ok).
    std::string Problem() const {
        if (!nameProblem.empty()) return nameProblem;
        if (!locationProblem.empty()) return locationProblem;
        return pathProblem;
    }
};

/// Work out where a project called `name` would go inside `location`, with every check the Create
/// button needs. Touches the disk only to look (and to try writing one probe file); makes nothing.
inline Plan PlanProject(const std::string& location, const std::string& name,
                        std::size_t maxRoot = kMaxRootPath, std::uintmax_t minFree = kMinFreeBytes) {
    Plan p;
    p.name = name;
    p.location = Absolute(fs::path(location));
    const NameCheck nc = CheckName(name);
    p.nameProblem = nc.problem;
    p.folder = nc.folder;
    p.locationProblem = CheckLocation(location.empty() ? fs::path() : p.location, minFree);
    if (!nc.ok()) { p.root = p.location; return p; }
    p.root = p.location / p.folder;
    for (int n = 2; LooksUsed(p.root) && n < 1000; ++n) {
        p.folder = nc.folder + "-" + std::to_string(n);
        p.root = p.location / p.folder;
        p.renamed = true;
    }
    if (p.root.string().size() > maxRoot)
        p.pathProblem = "The full path is " + std::to_string(p.root.string().size()) + " characters; keep it under " +
                        std::to_string(maxRoot) + ". Use a shorter name or location.";
    return p;
}

// ---- making and undoing -------------------------------------------------------------------------------------

/// What a creation step made, so a failure can take back exactly that and nothing else.
struct Made {
    std::vector<fs::path> paths;   ///< files and folders created, in creation order
    void Add(const fs::path& p) { paths.push_back(p); }
};

/// Make `dir` and any missing parents, recording each one that did not exist before.
inline bool MakeDirs(const fs::path& dir, Made& made, std::string& error) {
    std::error_code ec;
    std::vector<fs::path> missing;
    for (fs::path p = Absolute(dir); !p.empty() && !fs::exists(p, ec); ) {
        missing.push_back(p);
        fs::path up = p.parent_path();
        if (up == p) break;
        p = up;
    }
    for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
        ec.clear();
        if (!fs::create_directory(*it, ec) && ec) { error = "Couldn't create \"" + it->string() + "\": " + ec.message(); return false; }
        made.Add(*it);
    }
    if (!fs::is_directory(dir, ec)) { error = "\"" + dir.string() + "\" exists but isn't a folder."; return false; }
    return true;
}

/// Remove what `made` lists, newest first. A folder that still holds something (the person's files)
/// is left alone. Never removes anything it did not make.
inline void Undo(const Made& made) {
    std::error_code ec;
    for (auto it = made.paths.rbegin(); it != made.paths.rend(); ++it) {
        ec.clear();
        if (fs::is_directory(*it, ec)) { if (fs::is_empty(*it, ec)) fs::remove(*it, ec); }
        else fs::remove(*it, ec);
    }
}

/// <root>, <root>/Assets and the standard folders. On failure everything this call made is removed
/// again and `error` says what went wrong; on success `made` lists what was created.
inline bool MakeSkeleton(const fs::path& root, Made& made, std::string& error) {
    Made mine;
    bool ok = MakeDirs(root / "Assets", mine, error);
    if (ok) for (const std::string& f : StandardFolders()) { if (!(ok = MakeDirs(root / "Assets" / f, mine, error))) break; }
    if (ok) ok = MakeDirs(root / ".okay", mine, error);
    if (!ok) { Undo(mine); return false; }
    made.paths.insert(made.paths.end(), mine.paths.begin(), mine.paths.end());
    return true;
}


/// A folder a launcher (or a failed Create) left behind: only an empty Assets folder and nothing else.
inline bool IsEmptyShell(const fs::path& root) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return false;
    std::size_t n = 0;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        ++n;
        if (it->path().filename() != "Assets" || !fs::is_directory(it->path(), ec) || !fs::is_empty(it->path(), ec)) return false;
    }
    return !ec && n == 1;
}
/// Remove `root` when it is an empty shell (see IsEmptyShell). True if it was removed.
inline bool RemoveEmptyShell(const fs::path& root) {
    if (!IsEmptyShell(root)) return false;
    std::error_code ec;
    fs::remove(root / "Assets", ec);
    fs::remove(root, ec);
    return !fs::exists(root, ec);
}

// ---- writing files ------------------------------------------------------------------------------------------

/// Replace `path` with `text` all at once: write `path.tmp`, check it, rename it over. The old file stays
/// whole if anything fails (disk full, no permission, the folder is gone). A read-only target is refused
/// instead of overwritten. `error` says why.
inline bool WriteTextAtomic(const std::string& path, const std::string& text, std::string* error = nullptr) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    std::error_code ec;
    const fs::path target(path);
    if (fs::exists(target, ec) && !fs::is_directory(target, ec)) {
        const fs::perms perm = fs::status(target, ec).permissions();
        if (!ec && (perm & (fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write)) == fs::perms::none)
            return fail("\"" + target.filename().string() + "\" is read-only. Make it writable (or save under another name) and try again.");
    }
    if (target.has_parent_path() && !fs::is_directory(target.parent_path(), ec))
        return fail("The folder \"" + target.parent_path().string() + "\" doesn't exist.");
    const std::string tmp = path + ".tmp";
    {
        errno = 0;
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return fail("Couldn't write \"" + tmp + "\": " + (errno ? std::error_code(errno, std::generic_category()).message() : std::string("no permission")));
        f.write(text.data(), (std::streamsize)text.size());
        f.flush();
        if (!f) {
            const int e = errno;
            f.close();
            fs::remove(tmp, ec);
            return fail(std::string("Couldn't write \"") + path + "\": " + (e ? std::error_code(e, std::generic_category()).message() : std::string("the disk may be full")));
        }
    }
    const std::uintmax_t written = fs::file_size(tmp, ec);
    if (ec || written != text.size()) { fs::remove(tmp, ec); return fail("Couldn't write \"" + path + "\": the file came out the wrong size (is the disk full?)."); }
    fs::rename(tmp, target, ec);
    if (ec) {   // some platforms won't rename over an existing file: replace it by copying
        std::error_code ce;
        fs::copy_file(tmp, target, fs::copy_options::overwrite_existing, ce);
        std::error_code rc;
        fs::remove(tmp, rc);
        if (ce) return fail("Couldn't replace \"" + path + "\": " + ce.message());
    }
    return true;
}

/// Read a whole file; false if it can't be opened.
inline bool ReadText(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

/// Where the settings of the project in `root` live.
inline std::string SettingsPath(const std::string& root) {
    return root.empty() ? std::string(SettingsFileName()) : (fs::path(root) / SettingsFileName()).string();
}

}  // namespace okayproj
