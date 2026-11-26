// okay CLI: git through the person's installed git program. The only file that runs git. See Git.hpp.
// Written from the git documentation (git-status --porcelain=v1 -z, gitattributes, git-config). Our own code.
#include "Git.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "okay/Core/StringUtil.hpp"
#include "BuildSettingsModel.hpp"   // IgnoreLines / MergeIgnore: the editor's own .gitignore list and merge

namespace okaycli::vc {

std::vector<std::string> IgnoreLines(const std::string& buildOutputDir) {
    // The editor's Build Settings list first (/.okay/build_live.cfg, /.okay/build_history.txt, *.autosave, Thumbs.db,
    // .DS_Store, and the project's build output folder), so the two never disagree. Every line for a file or folder at a
    // fixed place starts with "/": it matches only at the project's top, never an Assets folder of the same name.
    std::vector<std::string> v = oked::bsm::IgnoreLines(buildOutputDir.empty() ? std::string("build") : buildOutputDir);
    for (const char* extra : {
             "/build/",   // the old default build output, also when Build Settings point somewhere else now
             // the project layout's folders that are made again or are not project data (docs/engine/project_layout.md)
             "/Library/", "/Logs/", "/Temp/", "/Builds/",
             // what the engine makes again by itself
             "/.okay/backups/", "/.okay/build_report.txt", "/.okay/upgrade_report.txt", "/.okay/upgrade_backup.txt",
             "/.okay/clashes/", ".okaycache/",
             // per-person editor files: what the Project panel shows and the favourites list
             "/.okay/projectview.txt", "/.okay/favourites.txt",
             // the copy the engine keeps of the previous version of a saved scene or prefab
             "*.okayscene.bak", "*.okayprefab.bak"}) {
        if (std::find(v.begin(), v.end(), extra) == v.end()) v.push_back(extra);
    }
    return v;
}

std::vector<std::string> AttributeLines() {
    std::vector<std::string> v = {
        "*.okayscene text eol=lf merge=okayscene diff=okayscene",
        "*.okayprefab text eol=lf merge=okayscene diff=okayscene",
    };
    for (const char* ext : {"png", "jpg", "jpeg", "tga", "bmp", "gif", "psd", "tif", "tiff", "hdr", "exr", "dds", "ktx", "ktx2",
                            "webp", "ico", "wav", "ogg", "mp3", "flac", "m4a", "opus", "aiff", "mp4", "webm", "mov", "fbx",
                            "glb", "bin", "blend", "3ds", "max", "ma", "mb", "ttf", "otf", "woff", "woff2", "zip", "7z", "exe",
                            "dll", "so", "dylib", "okaymesh", "okaypkg", "okaynav", "pdf"})
        v.push_back(std::string("*.") + ext + " binary");
    return v;
}

std::string GitIgnoreText(const std::string& existing, const std::string& buildOutputDir, int* added) {
    return oked::bsm::MergeIgnore(existing, IgnoreLines(buildOutputDir), added, "# added by okay vc init (files the engine makes again, and per-person files)");
}
std::string GitAttributesText(const std::string& existing, int* added) {
    return oked::bsm::MergeIgnore(existing, AttributeLines(), added,
                                  "# added by okay vc init (scenes merge with the engine's scene merge; pictures and sounds are binary)");
}

namespace {
/// The program path as git's shell (sh, also on Windows) reads it: forward slashes, in double quotes.
std::string ShellQuoted(std::string exe) {
    std::replace(exe.begin(), exe.end(), '\\', '/');
    std::string q = "\"";
    for (char c : exe) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') q += '\\';
        q += c;
    }
    return q + "\"";
}
} // namespace

std::string MergeDriverCommand(const std::string& okayExe) { return ShellQuoted(okayExe) + " vc merge-driver %O %A %B %P"; }
std::string DiffDriverCommand(const std::string& okayExe) { return ShellQuoted(okayExe) + " vc diff-driver"; }

// ---- status ------------------------------------------------------------------------------------------------------

const char* GroupName(Group g) {
    switch (g) {
        case Group::Scenes: return "Scenes and prefabs";
        case Group::Scripts: return "Scripts";
        case Group::Settings: return "Project settings";
        default: return "Assets";
    }
}

Group GroupOf(const std::string& path) {
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    const std::string lower = okay::str::LowerAscii(p);
    const std::size_t slash = lower.rfind('/');
    const std::string file = slash == std::string::npos ? lower : lower.substr(slash + 1);
    const std::size_t dot = file.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : file.substr(dot);
    if (ext == ".okayscene" || ext == ".okayprefab") return Group::Scenes;
    if (ext == ".okay" || ext == ".okayvs" || ext == ".okayscript" || ext == ".cs" || ext == ".lua" || ext == ".okaybt") return Group::Scripts;
    if (file == "project.okayproj" || file == ".gitignore" || file == ".gitattributes" || lower.rfind(".okay/", 0) == 0 || lower.rfind("projectsettings/", 0) == 0 ||
        ext == ".okayinput" || ext == ".okaybuild" || ext == ".okayconfig")
        return Group::Settings;
    return Group::Assets;
}

const char* StateWord(State s) {
    switch (s) {
        case State::New: return "new";
        case State::Deleted: return "deleted";
        case State::Renamed: return "renamed";
        case State::Conflict: return "clash";
        default: return "changed";
    }
}

std::vector<Change> ParseStatus(const std::string& z) {
    std::vector<Change> out;
    std::vector<std::string> fields;
    std::size_t at = 0;
    while (at < z.size()) {
        std::size_t end = z.find('\0', at);
        if (end == std::string::npos) end = z.size();
        fields.push_back(z.substr(at, end - at));
        at = end + 1;
    }
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const std::string& f = fields[i];
        if (f.size() < 4 || f[2] != ' ') continue;
        const char x = f[0], y = f[1];
        Change c;
        c.path = f.substr(3);
        if (x == '!' && y == '!') continue;   // ignored files
        const bool conflict = x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D');
        if (conflict) c.state = State::Conflict;
        else if (x == '?' && y == '?') c.state = State::New;
        else if (x == 'R' || x == 'C') {
            c.state = x == 'R' ? State::Renamed : State::New;
            if (i + 1 < fields.size()) c.from = fields[++i];   // -z: the old name follows as its own field
        } else if (x == 'A') c.state = State::New;
        else if (x == 'D' || y == 'D') c.state = State::Deleted;
        else c.state = State::Changed;
        c.group = GroupOf(c.path);
        out.push_back(c);
    }
    return out;
}

std::string StatusText(const std::vector<Change>& changes, const std::string& branch) {
    std::string t;
    t += branch.empty() ? "You are not on a named version line.\n" : "Version line: " + branch + "\n";
    if (changes.empty()) return t + "Everything is saved. Nothing has changed since the last save.\n";
    int conflicts = 0;
    for (const Change& c : changes) if (c.state == State::Conflict) ++conflicts;
    t += std::to_string(changes.size()) + (changes.size() == 1 ? " file has" : " files have") + " changed since the last save.\n";
    for (Group g : {Group::Scenes, Group::Scripts, Group::Assets, Group::Settings}) {
        std::vector<const Change*> in;
        for (const Change& c : changes) if (c.group == g) in.push_back(&c);
        if (in.empty()) continue;
        t += "\n" + std::string(GroupName(g)) + " (" + std::to_string(in.size()) + ")\n";
        for (const Change* c : in) {
            std::string word = StateWord(c->state);
            word.resize(9, ' ');
            t += "  " + word + c->path;
            if (!c->from.empty()) t += "  (was " + c->from + ")";
            t += "\n";
        }
    }
    if (conflicts)
        t += "\n" + std::to_string(conflicts) + (conflicts == 1 ? " file clashes" : " files clash") +
             " with changes from someone else. Open each one, keep what you want, then save with: okay vc save \"message\"\n";
    else t += "\nSave them with: okay vc save \"what you changed\"\n";
    return t;
}

// ---- running git -------------------------------------------------------------------------------------------------

std::string Git::Program() {
    const char* g = std::getenv("OKAY_GIT");
    return g && *g ? std::string(g) : std::string("git");
}

bool Git::Available() {
    const RunResult r = Run({Program(), "--version"}, "", true);
    return r.started && r.exitCode == 0;
}

std::string Git::Version() {
    const RunResult r = Run({Program(), "--version"}, "", true);
    if (!r.started || r.exitCode != 0) return "";
    return okay::str::Trim(r.out);
}

RunResult Git::Capture(const std::vector<std::string>& args) const {
    std::vector<std::string> argv = {Program()};
    argv.insert(argv.end(), args.begin(), args.end());
    return Run(argv, m_dir, true);
}

RunResult Git::Attached(const std::vector<std::string>& args) const {
    std::vector<std::string> argv = {Program()};
    argv.insert(argv.end(), args.begin(), args.end());
    return Run(argv, m_dir, false);
}

namespace {
std::string FirstLine(const std::string& s) { return okay::str::Trim(s.substr(0, s.find('\n'))); }
} // namespace

bool Git::IsRepo() const {
    const RunResult r = Capture({"rev-parse", "--is-inside-work-tree"});
    return r.started && r.exitCode == 0 && FirstLine(r.out) == "true";
}

std::string Git::TopLevel() const {
    const RunResult r = Capture({"rev-parse", "--show-toplevel"});
    return r.started && r.exitCode == 0 ? FirstLine(r.out) : std::string();
}

std::string Git::Branch() const {
    const RunResult r = Capture({"symbolic-ref", "--short", "-q", "HEAD"});
    return r.started && r.exitCode == 0 ? FirstLine(r.out) : std::string();
}

bool Git::HasCommits() const {
    const RunResult r = Capture({"rev-parse", "-q", "--verify", "HEAD"});
    return r.started && r.exitCode == 0;
}

std::vector<Change> Git::Status() const {
    const RunResult r = Capture({"status", "--porcelain=v1", "-z", "--untracked-files=all"});
    if (!r.started || r.exitCode != 0) return {};
    return ParseStatus(r.out);
}

std::string Git::Remote() const {
    const RunResult r = Capture({"remote", "get-url", "origin"});
    return r.started && r.exitCode == 0 ? FirstLine(r.out) : std::string();
}

} // namespace okaycli::vc
