// Upgrading a project: the backup, the check, the rewrite, the stamp, the restore, and the gate that asks first.
// See ProjectUpgrade.hpp and docs/project_upgrade.md. No ImGui here.
#include "ProjectUpgrade.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#include "ProjectState.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneMigration.hpp"
#include "okay/Scene/SceneSerializer.hpp"

namespace okayup {

// ---- small helpers -----------------------------------------------------------------------------------------------------

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool HasExt(const std::string& name, const char* ext) {
    const std::size_t n = std::strlen(ext);
    return name.size() > n && Lower(name.substr(name.size() - n)) == ext;
}
std::string ErrnoText() { return errno ? std::error_code(errno, std::generic_category()).message() : std::string("unknown error"); }

struct Fnv {
    std::uint64_t h = 1469598103934665603ull;
    void Update(const char* d, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) { h ^= static_cast<unsigned char>(d[i]); h *= 1099511628211ull; }
    }
};
std::uint64_t HashText(const std::string& t) { Fnv f; f.Update(t.data(), t.size()); return f.h; }

std::string Hex(std::uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(v));
    return b;
}
std::uint64_t ParseHex(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 16); }

// A path in the manifest: only '%', CR and LF need escaping.
std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '%') o += "%25"; else if (c == '\n') o += "%0A"; else if (c == '\r') o += "%0D"; else o += c;
    }
    return o;
}
std::string Unesc(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const std::string h = s.substr(i + 1, 2);
            if (h == "25") { o += '%'; i += 2; continue; }
            if (h == "0A") { o += '\n'; i += 2; continue; }
            if (h == "0D") { o += '\r'; i += 2; continue; }
        }
        o += s[i];
    }
    return o;
}

// Same rule the atomic writer applies: a file with no write bit set is refused, whoever is asking.
bool ReadOnly(const fs::path& p) {
    std::error_code ec;
    const fs::perms perm = fs::status(p, ec).permissions();
    return !ec && (perm & (fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write)) == fs::perms::none;
}

std::string Megabytes(std::uint64_t bytes) {
    const std::uint64_t mb = (bytes + 1024 * 1024 - 1) / (1024 * 1024);
    return std::to_string(mb) + " MB";
}

std::string NowUtc() {
    const std::time_t t = std::time(nullptr);
    const std::tm* tmv = std::gmtime(&t);   // the editor's UI thread only: the shared buffer is not shared
    char b[32] = "";
    if (tmv) std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", tmv);
    return b;
}

const char* kManifestRel = ".okay/upgrade_backup.txt";
const char* kManifestMagic = "okayspace-upgrade-backup 1";

bool SkipVcsDir(const std::string& name) { return name == ".git" || name == ".svn" || name == ".hg"; }

// A path as a comparison key without asking the disk anything: absolute, normalized, lower case on Windows.
std::string PathKey(const fs::path& p) {
    std::string s = okayproj::Absolute(p).lexically_normal().generic_string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#if defined(_WIN32)
    s = Lower(s);
#endif
    return s;
}

}  // namespace

// ---- which folder is the project ---------------------------------------------------------------------------------------

bool IsProjectRoot(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    return fs::is_directory(dir / "Assets", ec) || fs::exists(dir / okayproj::SettingsFileName(), ec) || fs::is_directory(dir / ".okay", ec);
}

std::string ProjectRootForScene(const std::string& scene) {
    if (scene.empty()) return {};
    std::error_code ec;
    fs::path abs = fs::absolute(fs::path(scene), ec);
    if (ec) return {};
    abs = abs.lexically_normal();
    for (fs::path p = abs.parent_path(); !p.empty(); p = p.parent_path()) {
        if (p.filename() == "Assets") return fs::is_directory(p, ec) ? p.parent_path().string() : std::string();
        if (p == p.parent_path()) break;
    }
    const fs::path dir = abs.parent_path();
    return IsProjectRoot(dir) ? dir.string() : std::string();
}

std::string RootKey(const std::string& root) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(root), ec);
    if (ec) p = okayproj::Absolute(fs::path(root));
    std::string s = p.lexically_normal().generic_string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#if defined(_WIN32)
    s = Lower(s);
#endif
    return s;
}

// ---- the stamp in project.okayproj ---------------------------------------------------------------------------------------

bool IsKnownProjectKey(const std::string& key) {
    if (key == "tag" || key == "strict_tags" || key.compare(0, 11, "layer_name_") == 0) return true;
    oked::ProjectSettings scratch;
    return oked::ApplyProjectSettingLine(scratch, key, "");
}

std::string ProjectFileText(const std::string& path, const std::string& generated, const std::string& running) {
    std::string existing;
    okayproj::ReadText(path, existing);
    return okayver::ComposeProjectFile(generated, existing, okayver::NormalizeVersion(running), IsKnownProjectKey);
}

bool StampProject(const std::string& projectDir, const std::string& running, std::string* error) {
    const std::string v = okayver::NormalizeVersion(running);
    if (v.empty() || projectDir.empty()) return true;
    std::error_code ec;
    if (!fs::exists(okayver::ProjectFile(projectDir), ec)) return true;
    return okayver::WriteProjectVersion(projectDir, v, error);
}

// ---- the job ----------------------------------------------------------------------------------------------------------------

const char* PhaseName(Phase p) {
    switch (p) {
        case Phase::Scanning: return "Looking through the project";
        case Phase::Checking: return "Checking scenes and prefabs";
        case Phase::Space: return "Checking free space";
        case Phase::Backup: return "Making the backup";
        case Phase::Verify: return "Checking the backup";
        case Phase::Applying: return "Upgrading files";
        case Phase::Done: return "Done";
        case Phase::Failed: return "Stopped";
    }
    return "";
}

std::string BackupFolderName(const std::string& projectName, const std::string& fromVersion, const std::string& toVersion) {
    const std::string from = okayver::NormalizeVersion(fromVersion), to = okayver::NormalizeVersion(toVersion);
    if (!from.empty()) return projectName + " (backup v" + from + ")";
    if (!to.empty()) return projectName + " (backup before v" + to + ")";
    return projectName + " (backup)";
}

namespace {

struct Entry {
    std::string rel;
    std::uint64_t size = 0;
    fs::file_time_type mtime{};
    bool isDir = false, isLink = false, isScene = false, isPrefab = false;
    std::uint64_t hash = 0;           // of the bytes the backup read
};

struct Change {
    std::string rel;
    std::uint64_t size = 0, hash = 0;
    fs::file_time_type mtime{};
    std::string text;                 // what to write
    bool isProject = false;
    bool existed = true;
};

fs::path FreeBackupPath(const fs::path& parent, const std::string& base) {
    std::error_code ec;
    fs::path p = parent / base;
    for (int n = 2; fs::exists(p, ec) && n < 1000; ++n) p = parent / (base + " (" + std::to_string(n) + ")");
    return p;
}

struct ManifestFile { std::string rel; std::uint64_t size = 0, hash = 0; };
struct Manifest {
    bool ok = false;
    std::string project, from, to, made, mode;
    std::uint64_t files = 0, bytes = 0;
    std::vector<ManifestFile> changed;
    std::vector<std::string> created;
};

Manifest ReadManifest(const fs::path& backupDir) {
    Manifest m;
    std::string text;
    if (!okayproj::ReadText((backupDir / kManifestRel).string(), text)) return m;
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) return m;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != kManifestMagic) return m;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "project") m.project = v;
        else if (k == "from") m.from = v;
        else if (k == "to") m.to = v;
        else if (k == "made") m.made = v;
        else if (k == "mode") m.mode = v;
        else if (k == "files") m.files = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "bytes") m.bytes = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "changed") {   // <size> <hash> <path>
            const std::size_t a = v.find(' '), b = a == std::string::npos ? a : v.find(' ', a + 1);
            if (b == std::string::npos) continue;
            ManifestFile f;
            f.size = std::strtoull(v.substr(0, a).c_str(), nullptr, 10);
            f.hash = ParseHex(v.substr(a + 1, b - a - 1));
            f.rel = Unesc(v.substr(b + 1));
            if (!f.rel.empty()) m.changed.push_back(f);
        } else if (k == "created") {
            if (!v.empty()) m.created.push_back(Unesc(v));
        }
    }
    m.ok = true;
    return m;
}

}  // namespace

struct UpgradeJob::Impl {
    Options opt;
    Result res;
    Phase phase = Phase::Scanning;
    bool cancelRequested = false;

    fs::path root, partial, finalDir, parent;
    std::string name;
    std::set<std::string> skipKeys;
    std::vector<Entry> entries;
    std::unique_ptr<fs::recursive_directory_iterator> scan;
    std::vector<std::size_t> candidates;
    std::vector<Change> changes;
    std::unique_ptr<okay::Scene> scratch;

    std::size_t idx = 0;                       // position inside the current phase
    std::uint64_t totalBytes = 0, copiedBytes = 0, totalFiles = 0, copiedFiles = 0, madeEntries = 0;
    std::string current;

    // the file being copied
    std::ifstream in;
    std::ofstream out;
    bool copying = false;
    std::uint64_t copyDone = 0;
    Fnv copyHash;
    std::vector<char> buf = std::vector<char>(1u << 20);

    std::vector<std::size_t> applied;          // indexes into `changes`, in the order written
    bool renamed = false;

    explicit Impl(Options o) : opt(std::move(o)) {
        root = fs::path(opt.projectDir).lexically_normal();
        if (root.filename().empty()) root = root.parent_path();
        name = root.filename().string();
        parent = opt.backupParent.empty() ? root.parent_path() : fs::path(opt.backupParent);
        const std::string base = BackupFolderName(name, opt.fromVersion, opt.toVersion);
        finalDir = FreeBackupPath(parent, base);
        partial = finalDir;
        partial += ".partial";
        skipKeys.insert(PathKey(partial));
        skipKeys.insert(PathKey(finalDir));
        for (const std::string& d : opt.skipDirs) if (!d.empty()) skipKeys.insert(PathKey(fs::path(d)));
    }

    bool Hook(const char* stage, const std::string& what) { return opt.fail && opt.fail(stage, what); }
    fs::path Abs(const std::string& rel) const { return root / fs::path(rel); }
    bool Finished() const { return phase == Phase::Done || phase == Phase::Failed; }

    // ---- failing and putting things back ----
    void Fail(const std::string& msg) {
        if (Finished()) return;
        res.ok = false;
        if (res.error.empty()) res.error = msg;
        CloseCopy();
        if (!renamed) {   // the backup was never finished: it goes, and the project was never touched
            std::error_code ec;
            fs::remove_all(partial, ec);
            if (ec) res.warnings.push_back("Couldn't remove the unfinished backup \"" + partial.string() + "\": " + ec.message());
            res.rolledBack = true;
            res.restoreNote = "Nothing in the project was changed.";
        } else {
            Rollback();
        }
        phase = Phase::Failed;
    }

    // Put every file written so far back from the finished backup, newest first.
    void Rollback() {
        std::vector<std::string> problems;
        for (std::size_t k = applied.size(); k-- > 0;) {
            const Change& c = changes[applied[k]];
            const fs::path target = Abs(c.rel);
            std::error_code ec;
            if (!c.existed) {   // the stamp made this file: take it away again
                if (Hook("restore", c.rel)) { problems.push_back(c.rel + ": the disk refused"); continue; }
                fs::remove(target, ec);
                if (ec && fs::exists(target, ec)) problems.push_back(c.rel + ": " + ec.message());
                continue;
            }
            std::string text, err;
            if (Hook("restore", c.rel) || !okayproj::ReadText((finalDir / fs::path(c.rel)).string(), text)) { problems.push_back(c.rel + ": the backup copy could not be read"); continue; }
            if (HashText(text) != c.hash) { problems.push_back(c.rel + ": the backup copy is damaged"); continue; }
            if (!okayproj::WriteTextAtomic(target.string(), text, &err)) problems.push_back(c.rel + ": " + err);
        }
        if (problems.empty()) {
            res.rolledBack = true;
            res.restoreNote = applied.empty() ? "Nothing in the project was changed."
                                              : "Every file that had been rewritten was put back from the backup: the project is exactly as it was.";
        } else {
            std::string m = "Couldn't put back everything. The original of each file is in the backup:";
            for (const std::string& p : problems) m += "\n  " + p;
            res.restoreNote = m;
        }
    }

    // ---- the units of work ----
    void Unit() {
        if (Finished()) return;
        if (cancelRequested && phase != Phase::Applying) { res.cancelled = true; Fail("Cancelled."); return; }
        switch (phase) {
            case Phase::Scanning: DoScan(); break;
            case Phase::Checking: DoCheck(); break;
            case Phase::Space: DoSpace(); break;
            case Phase::Backup: DoBackup(); break;
            case Phase::Verify: DoVerify(); break;
            case Phase::Applying: DoApply(); break;
            default: break;
        }
    }

    bool SkipDir(const fs::path& p, const std::string& rel, std::size_t depth, const std::string& nm) const {
        if (SkipVcsDir(nm)) return true;
        if (rel == ".okay/backups") return true;
        if (depth == 0 && Lower(nm) == "build") return true;     // build output (Build Settings' default folder)
        return skipKeys.count(PathKey(p)) != 0;
    }

    void DoScan() {
        std::error_code ec;
        if (!scan) {
            if (Hook("scan", root.string())) return Fail("Couldn't read the project folder: the disk refused.");
            if (opt.projectDir.empty() || !fs::is_directory(root, ec)) return Fail("The project folder \"" + root.string() + "\" isn't there.");
            if (opt.migrate && okayver::NormalizeVersion(opt.toVersion).empty())
                return Fail("This editor has no version number of its own (a development build), so there is nothing to upgrade the project to.");
            scan = std::make_unique<fs::recursive_directory_iterator>(root, ec);
            if (ec) return Fail("Couldn't read the project folder \"" + root.string() + "\": " + ec.message());
        }
        const fs::recursive_directory_iterator end;
        for (int n = 0; n < 256 && *scan != end; ++n) {
            const fs::directory_entry de = **scan;
            const fs::path rp = de.path().lexically_relative(root);   // not fs::relative: that would follow a link
            const std::string rel = rp.generic_string();
            const std::string nm = de.path().filename().string();
            const fs::file_status ls = de.symlink_status(ec);
            if (fs::is_symlink(ls)) {
                Entry e; e.rel = rel; e.isLink = true;
                entries.push_back(e);
            } else if (fs::is_directory(ls)) {
                if (SkipDir(de.path(), rel, static_cast<std::size_t>(scan->depth()), nm)) scan->disable_recursion_pending();
                else { Entry e; e.rel = rel; e.isDir = true; entries.push_back(e); }
            } else if (fs::is_regular_file(ls)) {
                Entry e;
                e.rel = rel;
                e.size = de.file_size(ec);
                if (ec) return Fail("Couldn't read \"" + rel + "\": " + ec.message());
                e.mtime = de.last_write_time(ec);
                e.isScene = HasExt(nm, ".okayscene");
                e.isPrefab = HasExt(nm, ".okayprefab");
                entries.push_back(e);
            } else {
                res.warnings.push_back("\"" + rel + "\" is not an ordinary file or folder: it is left out of the backup.");
            }
            current = rel;
            scan->increment(ec);
            if (ec) return Fail("Couldn't read the folder \"" + (root / rp).parent_path().string() + "\": " + ec.message());
        }
        if (*scan != end) return;
        scan.reset();
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.rel < b.rel; });
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.isDir || e.isLink) continue;
            totalBytes += e.size;
            ++totalFiles;
            if (opt.migrate && (e.isScene || e.isPrefab)) candidates.push_back(i);
        }
        idx = 0;
        phase = opt.migrate ? Phase::Checking : Phase::Space;
    }

    bool CheckWritableDirs() {
        std::set<std::string> dirs;
        for (const Change& c : changes) dirs.insert(Abs(c.rel).parent_path().string());
        for (const std::string& d : dirs) {
            std::string why;
            if (!okayproj::CanWriteIn(fs::path(d), &why)) {
                Fail("Can't write to \"" + d + "\" (" + (why.empty() ? std::string("read-only or no permission") : why) + "), so the project can't be upgraded. Nothing was changed.");
                return false;
            }
        }
        return true;
    }

    void DoCheck() {
        if (idx < candidates.size()) {
            Entry& e = entries[candidates[idx++]];
            current = e.rel;
            if (Hook("analyze", e.rel)) return Fail("Couldn't read \"" + e.rel + "\": the disk refused.");
            std::string text;
            const fs::path abs = Abs(e.rel);
            if (!okayproj::ReadText(abs.string(), text)) return Fail("Couldn't read \"" + e.rel + "\".");
            const okay::SceneMigration::Result r = okay::SceneMigration::MigrateThroughEngine(text);
            if (!r.ok) return Fail("\"" + e.rel + "\" can't be upgraded: " + r.error + ".");
            const std::string& finalText = r.changed ? r.text : text;
            if (!scratch) scratch = std::make_unique<okay::Scene>("upgrade check");
            std::string err;
            std::vector<std::string> warns;
            if (!okay::SceneSerializer::Deserialize(*scratch, finalText, &err, &warns))
                return Fail("\"" + e.rel + "\" can't be loaded" + (r.changed ? " after upgrading it" : "") + ": " + err + ".");
            if (!warns.empty()) res.log.push_back(e.rel + ": " + std::to_string(warns.size()) + (warns.size() == 1 ? " record" : " records") + " this version doesn't know (kept as they are).");
            (e.isPrefab ? res.prefabs : res.scenes)++;
            for (const std::string& l : r.log) res.log.push_back(e.rel + ": " + l);
            if (r.changed) {
                if (ReadOnly(abs)) return Fail("\"" + e.rel + "\" is read-only, so it can't be upgraded. Make it writable and try again. Nothing was changed.");
                Change c;
                c.rel = e.rel; c.size = e.size; c.mtime = e.mtime; c.hash = HashText(text); c.text = r.text;
                changes.push_back(std::move(c));
            }
            return;
        }
        // the stamp: project.okayproj last
        {
            const std::string to = okayver::NormalizeVersion(opt.toVersion);
            const fs::path pf = root / okayproj::SettingsFileName();
            std::error_code ec;
            std::string old;
            const bool exists = fs::exists(pf, ec) && !fs::is_directory(pf, ec);
            if (exists && !okayproj::ReadText(pf.string(), old)) return Fail("Couldn't read \"project.okayproj\".");
            const std::string text = okayver::SetStampInText(exists ? old : opt.legacySettings, to);
            if (!exists || text != old) {
                if (exists && ReadOnly(pf)) return Fail("\"project.okayproj\" is read-only, so the project can't be stamped with the new version. Make it writable and try again. Nothing was changed.");
                Change c;
                c.rel = okayproj::SettingsFileName(); c.isProject = true; c.existed = exists; c.text = text; c.hash = HashText(old);
                if (exists) { c.size = fs::file_size(pf, ec); c.mtime = fs::last_write_time(pf, ec); }
                changes.push_back(std::move(c));
            }
        }
        if (!changes.empty() && !CheckWritableDirs()) return;
        phase = Phase::Space;
    }

    void DoSpace() {
        if (Hook("space", parent.string())) return Fail("Couldn't check the free space: the disk refused.");
        std::error_code ec;
        if (!fs::is_directory(parent, ec)) return Fail("The folder for the backup, \"" + parent.string() + "\", isn't there.");
        std::string why;
        if (!okayproj::CanWriteIn(parent, &why))
            return Fail("Can't make the backup next to the project: \"" + parent.string() + "\" can't be written (" + (why.empty() ? std::string("read-only or no permission") : why) + "). Nothing was changed.");
        const std::uintmax_t need = static_cast<std::uintmax_t>(totalBytes) + opt.spaceMargin;
        std::uintmax_t have = static_cast<std::uintmax_t>(-1);
        std::error_code sec;
        if (opt.availableSpace) have = opt.availableSpace(parent, sec);
        else { const fs::space_info sp = fs::space(parent, sec); have = sp.available; }
        if (!sec && have != static_cast<std::uintmax_t>(-1) && have < need)
            return Fail("There isn't enough free space for the backup: it needs about " + Megabytes(need) + " and only " + Megabytes(have) +
                        " is free where the project is. Free some space and try again. Nothing was changed.");
        fs::remove_all(partial, ec);   // a leftover of an interrupted run (its name is ours)
        ec.clear();
        fs::create_directory(partial, ec);
        if (ec) return Fail("Couldn't start the backup folder \"" + partial.string() + "\": " + ec.message() + ". Nothing was changed.");
        idx = 0;
        phase = Phase::Backup;
    }

    void CloseCopy() {
        if (in.is_open()) in.close();
        if (out.is_open()) out.close();
        in.clear(); out.clear();
        copying = false;
    }

    void DoBackup() {
        if (idx >= entries.size()) { idx = 0; phase = Phase::Verify; return; }
        Entry& e = entries[idx];
        current = e.rel;
        std::error_code ec;
        const fs::path src = Abs(e.rel), dst = partial / fs::path(e.rel);
        if (!copying) {
            if (Hook("backup-copy", e.rel)) return Fail("Couldn't copy \"" + e.rel + "\" to the backup: the disk refused. Nothing in the project was changed.");
            if (e.isDir) {
                fs::create_directories(dst, ec);
                if (ec) return Fail("Couldn't make the backup folder \"" + dst.string() + "\": " + ec.message() + ". Nothing in the project was changed.");
                ++madeEntries; ++idx; return;
            }
            if (e.isLink) {
                fs::create_directories(dst.parent_path(), ec);
                ec.clear();
                fs::copy_symlink(src, dst, ec);
                if (ec) res.warnings.push_back("The link \"" + e.rel + "\" was not copied into the backup (" + ec.message() + ").");
                else ++madeEntries;
                ++idx; return;
            }
            fs::create_directories(dst.parent_path(), ec);
            ec.clear();
            errno = 0;
            in.open(src, std::ios::binary);
            if (!in) return Fail("Couldn't read \"" + e.rel + "\" for the backup: " + ErrnoText() + ". Nothing in the project was changed.");
            errno = 0;
            out.open(dst, std::ios::binary | std::ios::trunc);
            if (!out) return Fail("Couldn't write \"" + dst.string() + "\": " + ErrnoText() + ". Nothing in the project was changed.");
            copying = true; copyDone = 0; copyHash = Fnv{};
        }
        const std::uint64_t want = std::min<std::uint64_t>(buf.size(), e.size - copyDone);
        if (want > 0) {
            in.read(buf.data(), static_cast<std::streamsize>(want));
            const std::streamsize got = in.gcount();
            if (got <= 0 || in.bad()) return Fail("Couldn't read \"" + e.rel + "\" for the backup (it changed while it was being copied?). Nothing in the project was changed.");
            out.write(buf.data(), got);
            if (!out) return Fail("Couldn't write \"" + dst.string() + "\": " + ErrnoText() + " (is the disk full?). Nothing in the project was changed.");
            copyHash.Update(buf.data(), static_cast<std::size_t>(got));
            copyDone += static_cast<std::uint64_t>(got);
            copiedBytes += static_cast<std::uint64_t>(got);
        }
        if (copyDone < e.size) return;
        // the file is in; it must also have ended where it was when the project was scanned
        char probe;
        in.read(&probe, 1);
        const bool grew = in.gcount() > 0;
        out.flush();
        const bool wrote = static_cast<bool>(out);
        CloseCopy();
        if (!wrote) return Fail("Couldn't write \"" + dst.string() + "\": " + ErrnoText() + " (is the disk full?). Nothing in the project was changed.");
        if (grew) return Fail("\"" + e.rel + "\" changed while it was being copied. Close any program that is using the project and try again. Nothing in the project was changed.");
        e.hash = copyHash.h;
        fs::last_write_time(dst, e.mtime, ec);   // best effort: the copy keeps its date
        ++copiedFiles; ++madeEntries; ++idx;
    }

    std::string ManifestText() const {
        std::string t = std::string(kManifestMagic) + "\n";
        t += "project=" + name + "\n";
        t += "from=" + okayver::NormalizeVersion(opt.fromVersion) + "\n";
        t += "to=" + okayver::NormalizeVersion(opt.toVersion) + "\n";
        t += std::string("mode=") + (opt.migrate ? "upgrade" : "backup") + "\n";
        t += "made=" + NowUtc() + "\n";
        t += "files=" + std::to_string(copiedFiles) + "\n";
        t += "bytes=" + std::to_string(copiedBytes) + "\n";
        for (const Change& c : changes) {
            if (c.existed) t += "changed=" + std::to_string(c.size) + " " + Hex(c.hash) + " " + Esc(c.rel) + "\n";
            else t += "created=" + Esc(c.rel) + "\n";
        }
        return t;
    }

    void DoVerify() {
        std::error_code ec;
        if (idx == 0 && Hook("backup-verify", partial.string())) return Fail("Couldn't check the backup: the disk refused. Nothing in the project was changed.");
        for (int n = 0; n < 256 && idx < entries.size(); ++n, ++idx) {
            const Entry& e = entries[idx];
            if (e.isLink) continue;
            const fs::path dst = partial / fs::path(e.rel);
            current = e.rel;
            if (e.isDir) {
                if (!fs::is_directory(dst, ec)) return Fail("The backup is missing the folder \"" + e.rel + "\". Nothing in the project was changed.");
            } else {
                const std::uintmax_t sz = fs::is_regular_file(dst, ec) ? fs::file_size(dst, ec) : static_cast<std::uintmax_t>(-1);
                if (ec || sz != e.size) return Fail("The backup copy of \"" + e.rel + "\" came out the wrong size. Nothing in the project was changed.");
            }
        }
        if (idx < entries.size()) return;
        std::uint64_t count = 0;
        for (fs::recursive_directory_iterator it(partial, ec), end; !ec && it != end; it.increment(ec)) ++count;
        if (ec || count != madeEntries) return Fail("The backup holds " + std::to_string(count) + " items but " + std::to_string(madeEntries) + " were copied. Nothing in the project was changed.");
        for (const Change& c : changes) {   // what will be rewritten must be in the backup exactly, and must not have changed since it was checked
            if (!c.existed) continue;
            std::string text;
            if (!okayproj::ReadText((partial / fs::path(c.rel)).string(), text) || HashText(text) != c.hash)
                return Fail("\"" + c.rel + "\" changed while it was being backed up. Close any program that is using the project and try again. Nothing in the project was changed.");
            const auto it = std::lower_bound(entries.begin(), entries.end(), c.rel, [](const Entry& e, const std::string& r) { return e.rel < r; });
            if (it == entries.end() || it->rel != c.rel || it->hash != c.hash)
                return Fail("\"" + c.rel + "\" changed while it was being backed up. Close any program that is using the project and try again. Nothing in the project was changed.");
        }
        if (Hook("backup-manifest", partial.string())) return Fail("Couldn't finish the backup: the disk refused. Nothing in the project was changed.");
        std::string err;
        fs::create_directories(partial / ".okay", ec);
        if (!okayproj::WriteTextAtomic((partial / kManifestRel).string(), ManifestText(), &err)) return Fail("Couldn't finish the backup: " + err + " Nothing in the project was changed.");
        if (Hook("backup-rename", partial.string())) return Fail("Couldn't finish the backup: the disk refused. Nothing in the project was changed.");
        finalDir = FreeBackupPath(parent, BackupFolderName(name, opt.fromVersion, opt.toVersion));   // the name may have been taken meanwhile
        fs::rename(partial, finalDir, ec);
        if (ec) return Fail("Couldn't finish the backup folder \"" + finalDir.string() + "\": " + ec.message() + ". Nothing in the project was changed.");
        renamed = true;
        res.backupDir = finalDir.string();
        res.files = copiedFiles;
        res.bytes = copiedBytes;
        idx = 0;
        phase = opt.migrate ? Phase::Applying : Phase::Done;
        if (phase == Phase::Done) res.ok = true;
    }

    void DoApply() {
        if (idx >= changes.size()) {
            for (const Change& c : changes) { res.changed.push_back(c.rel); if (!c.isProject) ++res.rewritten; }
            res.ok = true;
            phase = Phase::Done;
            return;
        }
        const std::size_t k = idx;
        const Change& c = changes[k];
        const fs::path target = Abs(c.rel);
        current = c.rel;
        std::error_code ec;
        if (Hook(c.isProject ? "stamp" : "apply", c.rel)) return Fail("Couldn't write \"" + c.rel + "\": the disk refused.");
        if (c.existed) {
            const std::uintmax_t sz = fs::file_size(target, ec);
            const fs::file_time_type mt = fs::last_write_time(target, ec);
            if (ec || sz != c.size || mt != c.mtime) return Fail("\"" + c.rel + "\" changed while it was being upgraded. Close any program that is using the project and try again.");
        } else if (fs::exists(target, ec)) {
            return Fail("\"" + c.rel + "\" appeared while the project was being upgraded. Close any program that is using the project and try again.");
        }
        std::string err;
        if (!okayproj::WriteTextAtomic(target.string(), c.text, &err)) return Fail(err);
        applied.push_back(k);
        ++idx;
    }

    Progress Prog() const {
        Progress p;
        p.phase = phase;
        p.file = current;
        const double wScan = 0.05, wCheck = opt.migrate ? 0.15 : 0.0, wBackup = 0.60, wVerify = 0.10, wApply = opt.migrate ? 0.10 : 0.0;
        const double sum = wScan + wCheck + wBackup + wVerify + wApply;
        auto ratio = [](double a, double b) { return b > 0 ? std::min(1.0, a / b) : 1.0; };
        double f = 0;
        switch (phase) {
            case Phase::Scanning: p.done = entries.size(); f = 0; break;
            case Phase::Checking: p.done = idx; p.total = candidates.size(); f = wScan + wCheck * ratio((double)idx, (double)candidates.size()); break;
            case Phase::Space: f = wScan + wCheck; break;
            case Phase::Backup: p.done = copiedFiles; p.total = totalFiles;
                f = wScan + wCheck + wBackup * (totalBytes > 0 ? ratio((double)copiedBytes, (double)totalBytes) : ratio((double)idx, (double)entries.size())); break;
            case Phase::Verify: p.done = idx; p.total = entries.size(); f = wScan + wCheck + wBackup + wVerify * ratio((double)idx, (double)entries.size()); break;
            case Phase::Applying: p.done = idx; p.total = changes.size(); f = wScan + wCheck + wBackup + wVerify + wApply * ratio((double)idx, (double)changes.size()); break;
            case Phase::Done: p.done = p.total = changes.size(); f = sum; break;
            case Phase::Failed: f = 0; break;
        }
        p.fraction = phase == Phase::Failed ? 0.0 : std::min(1.0, f / sum);
        return p;
    }
};

UpgradeJob::UpgradeJob(Options opt) : m(std::make_unique<Impl>(std::move(opt))) {}
UpgradeJob::~UpgradeJob() = default;

bool UpgradeJob::Step(std::chrono::milliseconds budget) {
    if (m->Finished()) return false;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    do { m->Unit(); } while (!m->Finished() && std::chrono::steady_clock::now() < deadline);
    return !m->Finished();
}
void UpgradeJob::Run() { while (!m->Finished()) m->Unit(); }
void UpgradeJob::Cancel() { m->cancelRequested = true; }
bool UpgradeJob::Finished() const { return m->Finished(); }
Progress UpgradeJob::progress() const { return m->Prog(); }
const Result& UpgradeJob::result() const { return m->res; }

// ---- backups --------------------------------------------------------------------------------------------------------------

std::vector<BackupInfo> ListBackups(const std::string& projectDir) {
    std::vector<BackupInfo> out;
    fs::path root = fs::path(projectDir).lexically_normal();
    if (root.filename().empty()) root = root.parent_path();
    const std::string prefix = root.filename().string() + " (backup";
    std::error_code ec;
    for (fs::directory_iterator it(root.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        const std::string nm = it->path().filename().string();
        if (nm.compare(0, prefix.size(), prefix) != 0) continue;
        if (nm.size() > 8 && nm.compare(nm.size() - 8, 8, ".partial") == 0) continue;   // unfinished: never offered
        BackupInfo b;
        b.dir = it->path();
        b.name = nm;
        const Manifest m = ReadManifest(b.dir);
        b.restorable = m.ok;
        b.fromVersion = m.from; b.toVersion = m.to; b.made = m.made; b.mode = m.mode;
        b.files = m.files; b.bytes = m.bytes;
        for (const ManifestFile& f : m.changed) b.changed.push_back(f.rel);
        b.created = m.created;
        out.push_back(std::move(b));
    }
    std::sort(out.begin(), out.end(), [](const BackupInfo& a, const BackupInfo& b) { return a.name < b.name; });
    return out;
}

RestoreResult RestoreBackup(const BackupInfo& backup, const std::string& projectDir, const FailHook& fail) {
    RestoreResult r;
    auto hook = [&](const std::string& what) { return fail && fail("restore", what); };
    const Manifest m = ReadManifest(backup.dir);
    if (!m.ok) { r.error = "\"" + backup.dir.string() + "\" isn't a backup this editor made, so it can't be restored automatically. Its files are all there to copy by hand."; return r; }
    const fs::path root = fs::path(projectDir);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) { r.error = "The project folder isn't there."; return r; }
    if (m.mode == "backup") { r.error = "This backup was made before opening a newer project as it is. Nothing was rewritten, so there is nothing to restore."; return r; }

    // 1. the backup itself must be whole
    std::vector<std::string> texts;
    for (const ManifestFile& f : m.changed) {
        std::string t;
        if (!okayproj::ReadText((backup.dir / fs::path(f.rel)).string(), t) || t.size() != f.size || HashText(t) != f.hash) {
            r.error = "The backup is damaged: \"" + f.rel + "\" is missing or doesn't match. Nothing was changed.";
            return r;
        }
        texts.push_back(std::move(t));
    }
    // 2. everything about to be replaced is kept first
    fs::path side = root / ".okay" / "backups" / "restore-1";
    for (int n = 2; fs::exists(side, ec) && n < 1000; ++n) side = root / ".okay" / "backups" / ("restore-" + std::to_string(n));
    r.sideCopyDir = side.string();
    std::set<std::string> hadBefore;
    auto saveSide = [&](const std::string& rel) {
        const fs::path target = root / fs::path(rel);
        if (!fs::exists(target, ec)) return true;
        if (hook(rel)) return false;
        std::error_code e2;
        fs::create_directories((side / fs::path(rel)).parent_path(), e2);
        fs::copy_file(target, side / fs::path(rel), fs::copy_options::overwrite_existing, e2);
        if (e2) return false;
        hadBefore.insert(rel);
        return true;
    };
    for (const ManifestFile& f : m.changed) if (!saveSide(f.rel)) { fs::remove_all(side, ec); r.error = "Couldn't keep a copy of \"" + f.rel + "\" before replacing it. Nothing was changed."; return r; }
    for (const std::string& c : m.created) if (!saveSide(c)) { fs::remove_all(side, ec); r.error = "Couldn't keep a copy of \"" + c + "\" before removing it. Nothing was changed."; return r; }

    // 3. put the files back; on a failure put the kept copies back
    std::vector<std::string> done;
    auto undo = [&]() {
        for (std::size_t i = done.size(); i-- > 0;) {
            const fs::path target = root / fs::path(done[i]);
            std::error_code e2;
            if (hadBefore.count(done[i])) fs::copy_file(side / fs::path(done[i]), target, fs::copy_options::overwrite_existing, e2);
            else fs::remove(target, e2);
        }
    };
    for (std::size_t i = 0; i < m.changed.size(); ++i) {
        const std::string& rel = m.changed[i].rel;
        const fs::path target = root / fs::path(rel);
        std::string err;
        fs::create_directories(target.parent_path(), ec);
        if (hook(rel) || !okayproj::WriteTextAtomic(target.string(), texts[i], &err)) {
            undo();
            fs::remove_all(side, ec);
            r.error = "Couldn't restore \"" + rel + "\"" + (err.empty() ? std::string(".") : ": " + err) + " The files were put back as they were before the restore.";
            return r;
        }
        done.push_back(rel);
        ++r.restored;
    }
    for (const std::string& rel : m.created) {
        const fs::path target = root / fs::path(rel);
        if (!fs::exists(target, ec)) continue;
        if (hook(rel)) { undo(); fs::remove_all(side, ec); r.error = "Couldn't remove \"" + rel + "\". The files were put back as they were before the restore."; return r; }
        fs::remove(target, ec);
        if (ec) { const std::string why = ec.message(); undo(); fs::remove_all(side, ec); r.error = "Couldn't remove \"" + rel + "\": " + why + ". The files were put back as they were before the restore."; return r; }
        done.push_back(rel);
        ++r.removed;
    }
    r.ok = true;
    r.note = "Put back " + std::to_string(r.restored) + (r.restored == 1 ? " file" : " files") +
             (r.removed ? " and removed " + std::to_string(r.removed) : std::string()) + ". The files that were replaced are kept in \"" + side.string() + "\".";
    return r;
}

// ---- the gate ---------------------------------------------------------------------------------------------------------------

ProjectGate::ProjectGate(GateEnv env) : env_(std::move(env)) {}

void ProjectGate::MarkCleared(const std::string& root) { if (!root.empty()) { cleared_.insert(RootKey(root)); ++revision_; } }
bool ProjectGate::IsCleared(const std::string& root) const { return !root.empty() && cleared_.count(RootKey(root)) != 0; }
bool ProjectGate::IsKept(const std::string& root) const { return !root.empty() && kept_.count(RootKey(root)) != 0; }

void ProjectGate::NoteKeepRequest(const std::string& scene, const std::string& forVersion) {
    const std::string root = ProjectRootForScene(scene);
    if (root.empty()) return;
    if (!forVersion.empty() && okayver::CompareVersions(forVersion, env_.running) != 0) return;   // meant for another version's editor
    kept_.insert(RootKey(root));
    ++revision_;
}

bool ProjectGate::MayLoad(const std::string& scene, std::string* why) {
    const std::string root = ProjectRootForScene(scene);
    if (root.empty() || IsCleared(root)) return true;
    const okayver::Stamp st = okayver::ReadProjectStamp(root);
    const okayver::Decision d = okayver::Decide(st.version, env_.running);
    if (d == okayver::Decision::Same || d == okayver::Decision::SamePatchLine || d == okayver::Decision::DevBuild ||
        (IsKept(root) && (d == okayver::Decision::Older || d == okayver::Decision::Unknown))) {
        MarkCleared(root);
        return true;
    }
    if (why) {
        *why = d == okayver::Decision::Newer ? "This project was last saved by " + okayver::DisplayVersion(st.version) + ", which is newer than this editor, and you haven't chosen how to open it yet."
             : d == okayver::Decision::Older ? "This project was made with " + okayver::DisplayVersion(st.version) + " and you haven't chosen whether to upgrade it yet."
                                             : "This project has no version recorded and you haven't chosen whether to upgrade it yet.";
        *why += " Open it from File > Open Project Folder.";
    }
    return false;
}

void ProjectGate::Open(const std::string& scene, std::function<void()> proceed, std::function<void()> cancel) {
    if (Busy()) { if (cancel) cancel(); return; }
    const std::string root = ProjectRootForScene(scene);
    if (root.empty() || IsCleared(root)) { if (proceed) proceed(); return; }
    const okayver::Stamp st = okayver::ReadProjectStamp(root);
    const okayver::Decision d = okayver::Decide(st.version, env_.running);
    switch (d) {
        case okayver::Decision::Same:
        case okayver::Decision::SamePatchLine:   // another patch of this line: formats never change in a patch, so nothing to ask
        case okayver::Decision::DevBuild:
            MarkCleared(root);
            if (proceed) proceed();
            return;
        case okayver::Decision::Older:
        case okayver::Decision::Unknown:
            if (IsKept(root)) { MarkCleared(root); if (proceed) proceed(); return; }
            proceed_ = std::move(proceed); cancel_ = std::move(cancel);
            Ask(d == okayver::Decision::Older ? PromptKind::Older : PromptKind::Unknown, root, scene, st.version);
            return;
        case okayver::Decision::Newer:
            proceed_ = std::move(proceed); cancel_ = std::move(cancel);
            Ask(PromptKind::Newer, root, scene, st.version);
            return;
    }
}

void ProjectGate::Upgrade(const std::string& scene, std::function<void()> done) {
    if (Busy()) return;
    const std::string root = ProjectRootForScene(scene);
    if (root.empty()) return;
    const okayver::Stamp st = okayver::ReadProjectStamp(root);
    proceed_ = std::move(done);
    cancel_ = nullptr;
    Ask(st.version.empty() ? PromptKind::Unknown : PromptKind::Older, root, scene, st.version);
    StartJob(true);
}

void ProjectGate::Ask(PromptKind k, const std::string& root, const std::string& scene, const std::string& version) {
    prompt_ = Prompt{};
    prompt_.kind = k;
    prompt_.root = root;
    prompt_.scene = scene;
    prompt_.projectName = fs::path(root).filename().string();
    prompt_.projectVersion = version;
    prompt_.runningVersion = okayver::NormalizeVersion(env_.running);
    if (k == PromptKind::Older && env_.launcherDir && !env_.launcherDir().empty())
        prompt_.keepTarget = okayver::ResolveKeepTarget(version, okayver::InstalledVersions(env_.launcherDir()));
}

void ProjectGate::StartJob(bool migrate) {
    Options o;
    o.projectDir = prompt_.root;
    o.fromVersion = prompt_.projectVersion;
    o.toVersion = prompt_.runningVersion;
    o.migrate = migrate;
    if (env_.legacySettings) o.legacySettings = env_.legacySettings();
    if (env_.outputDirs) o.skipDirs = env_.outputDirs(prompt_.root);
    o.fail = env_.fail;
    o.availableSpace = env_.availableSpace;
    job_ = std::make_unique<UpgradeJob>(std::move(o));
    const std::string root = prompt_.root, scene = prompt_.scene, name = prompt_.projectName, from = prompt_.projectVersion, run = prompt_.runningVersion;
    prompt_ = Prompt{};
    prompt_.kind = PromptKind::Working;
    prompt_.root = root; prompt_.scene = scene; prompt_.projectName = name; prompt_.projectVersion = from; prompt_.runningVersion = run;
    prompt_.backupOnly = !migrate;
}

void ProjectGate::Finish(bool proceed) {
    auto go = std::move(proceed_), no = std::move(cancel_);
    proceed_ = nullptr; cancel_ = nullptr;
    job_.reset();
    prompt_ = Prompt{};
    if (proceed) { if (go) go(); }
    else if (no) no();
}

void ProjectGate::KeepVersion() {
    const std::string ver = prompt_.projectVersion;
    const fs::path launcher = env_.launcherDir ? env_.launcherDir() : fs::path();
    // "Keep v2.850.x": the newest installed editor of the project's line (a 2.850.0 project keeps 2.850.1 if that is installed)
    const std::string target = ver.empty() || launcher.empty() ? std::string() : okayver::ResolveKeepTarget(ver, okayver::InstalledVersions(launcher));
    if (!target.empty()) {
        Launch l;
        const fs::path exe = okayver::InstalledEditorPath(launcher, target);
        l.exe = exe.string();
        l.args = okayver::KeepVersionArgs(prompt_.scene);
        for (const std::string& a : env_.forwardArgs) l.args.push_back(a);
        l.workDir = exe.parent_path().string();
        launch_ = std::move(l);
        quit_ = true;
        Finish(false);
        return;
    }
    prompt_.kind = PromptKind::NotInstalled;
    prompt_.message = okayver::LineLabel(ver) + " is not installed. Open the OkaySpace launcher > Versions to install it.";
}

void ProjectGate::Answer(Choice c) {
    switch (prompt_.kind) {
        case PromptKind::Older:
            if (c == Choice::Upgrade) StartJob(true);
            else if (c == Choice::Keep) KeepVersion();
            else if (c == Choice::Cancel) Finish(false);
            return;
        case PromptKind::Unknown:
            if (c == Choice::Upgrade) StartJob(true);
            else if (c == Choice::Cancel) Finish(false);
            return;
        case PromptKind::Newer:
            if (c == Choice::OpenAnyway) StartJob(false);
            else if (c == Choice::UpdateEditor) { update_ = true; Finish(false); }
            else if (c == Choice::Cancel) Finish(false);
            return;
        case PromptKind::NotInstalled:
            if (c == Choice::Back) { prompt_.kind = PromptKind::Older; prompt_.message.clear(); }
            else if (c == Choice::Cancel || c == Choice::Close) Finish(false);
            return;
        case PromptKind::Working:
            if (c == Choice::Cancel && job_) job_->Cancel();
            return;
        case PromptKind::Done:
            if (c == Choice::Continue) { MarkCleared(prompt_.root); Finish(true); }
            return;
        case PromptKind::Failed:
            if (c == Choice::Close || c == Choice::Cancel) Finish(false);
            return;
        case PromptKind::None:
            return;
    }
}

void ProjectGate::Tick(std::chrono::milliseconds budget) {
    if (prompt_.kind != PromptKind::Working || !job_) return;
    job_->Step(budget);
    prompt_.progress = job_->progress();
    if (!job_->Finished()) return;
    prompt_.result = job_->result();
    if (prompt_.result.cancelled) { Finish(false); return; }
    prompt_.kind = prompt_.result.ok ? PromptKind::Done : PromptKind::Failed;
}

}  // namespace okayup
