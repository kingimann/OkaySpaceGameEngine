// okay vc: version control with the person's installed git underneath, and the merge and diff drivers git calls for
// scenes and prefabs. Our own code, written on the engine's APIs (SceneMerge, SceneDiff, SceneMigration,
// SceneSerializer, ProjectState) and the git documentation (gitattributes "Defining a custom merge driver" and
// "Defining an external diff driver", git-config, git-ls-files -u and the index stages :1: :2: :3:, git-rebase's
// swapped "ours" and "theirs"). git is only run through Git.cpp / Process.cpp.
#include "Cli.hpp"
#include "CliInternal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <system_error>

#include "Git.hpp"
#include "Process.hpp"
#include "ProjectState.hpp"     // editor/: the Build Settings file (where the build output goes)
#include "ProjectCreate.hpp"    // shared/: WriteTextAtomic, ReadText
#include "okay/Core/StringUtil.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneDiff.hpp"
#include "okay/Scene/SceneMerge.hpp"
#include "okay/Scene/SceneMigration.hpp"
#include "okay/Scene/SceneSerializer.hpp"

namespace okaycli {
namespace fs = std::filesystem;
using okay::str::Trim;
using namespace detail;

namespace {

// ---- small helpers -------------------------------------------------------------------------------------------

/// The git folder for a vc command: the project, else this folder (vc works in any folder).
std::string VcFolder(const std::string& project) {
    const std::string root = FindProjectRoot(project.empty() ? "." : project);
    if (!root.empty()) return root;
    std::error_code ec;
    return fs::absolute(project.empty() ? "." : project, ec).string();
}

/// A file named on the command line: relative names are inside the -C / --project folder when one was given (else
/// this folder), as every other vc command reads them.
fs::path FileArg(const std::string& file, const std::string& project) {
    std::error_code ec;
    const fs::path f(file);
    if (f.is_absolute()) return f.lexically_normal();
    const fs::path base = fs::absolute(project.empty() ? "." : project, ec);
    return (base / f).lexically_normal();
}

std::string Canon(const std::string& p) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(fs::absolute(p, ec), ec);
    std::string s = c.generic_string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#if defined(_WIN32)
    s = okay::str::LowerAscii(s);
#endif
    return s;
}

bool NeedGit(const Io& io) {
    if (vc::Git::Available()) return true;
    io.Warn("Git is not installed (or not on PATH). Install it from git-scm.com, then try again.");
    return false;
}

/// git's own words when it refused, kept short.
std::string GitSaid(const RunResult& r) {
    std::string e = Trim(r.err.empty() ? r.out : r.err);
    if (e.size() > 600) e = e.substr(0, 600) + " ...";
    return e.empty() ? std::string() : "\nGit said: " + e;
}

bool IdentityProblem(const RunResult& r) {
    return r.err.find("Please tell me who you are") != std::string::npos || r.err.find("empty ident") != std::string::npos ||
           r.err.find("unable to auto-detect email") != std::string::npos;
}
const char* kWhoHint = "Git needs your name and email once, to sign your saves. Run: okay vc who \"Your Name\" you@example.com";

std::string Stamp() {
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
    return stamp;
}

// ---- the driver settings in .git/config -------------------------------------------------------------------------

/// The program at the front of a stored driver command ("\"/x/okay\" vc merge-driver ..." -> /x/okay).
std::string ProgramOf(const std::string& command) {
    std::string p;
    if (command.empty()) return p;
    if (command[0] != '"') return command.substr(0, command.find(' '));
    for (std::size_t i = 1; i < command.size(); ++i) {
        if (command[i] == '\\' && i + 1 < command.size()) { p += command[++i]; continue; }
        if (command[i] == '"') break;
        p += command[i];
    }
    return p;
}

bool SetDrivers(const vc::Git& git, const std::string& okayExe, std::string* why) {
    const std::vector<std::vector<std::string>> config = {
        {"config", "--local", "merge.okayscene.name", "OkaySpace scene merge"},
        {"config", "--local", "merge.okayscene.driver", vc::MergeDriverCommand(okayExe)},
        {"config", "--local", "diff.okayscene.command", vc::DiffDriverCommand(okayExe)},
    };
    for (const auto& c : config) {
        const RunResult r = git.Capture(c);
        if (!r.started || r.exitCode != 0) { if (why) *why = GitSaid(r); return false; }
    }
    return true;
}

/// Every okay vc command checks this: when the stored drivers point at another okay (a version the launcher removed,
/// a moved install), they are pointed at the stable okay again, so `git diff` and merges keep using the engine.
void RepairDrivers(const vc::Git& git, const Io& io) {
    const std::string stored = Trim(git.Capture({"config", "--local", "--get", "merge.okayscene.driver"}).out);
    if (stored.empty()) return;   // not set up by okay vc init: leave it alone
    const std::string okayExe = StableOkayPath();
    if (okayExe.empty() || stored == vc::MergeDriverCommand(okayExe)) return;
    std::error_code ec;
    const std::string old = ProgramOf(stored);
    if (!SetDrivers(git, okayExe, nullptr)) return;
    if (!fs::exists(old, ec)) io.Warn("Git's scene merge pointed at an okay program that is gone (" + old + "); it uses " + okayExe + " now.");
}

bool NeedRepo(const vc::Git& git, const Io& io) {
    if (!NeedGit(io)) return false;
    if (!git.IsRepo()) {
        io.Warn("Version control is not started in this project yet. Start it with: okay vc init");
        return false;
    }
    RepairDrivers(git, io);
    return true;
}

// ---- merges in progress and clash reports ------------------------------------------------------------------------

bool GitPathIsDir(const vc::Git& git, const std::string& name) {
    const std::string p = Trim(git.Capture({"rev-parse", "--git-path", name}).out);
    if (p.empty()) return false;
    std::error_code ec;
    fs::path full = fs::path(p);
    if (!full.is_absolute()) full = fs::path(git.Folder().empty() ? "." : git.Folder()) / full;
    return fs::is_directory(full, ec);
}

/// A rebase is running: git swaps the sides then ("ours" is the line being rebased onto, "theirs" is your own save).
bool InRebase(const vc::Git& git) { return GitPathIsDir(git, "rebase-merge") || GitPathIsDir(git, "rebase-apply"); }

bool MergeInProgress(const vc::Git& git) {
    for (const char* ref : {"MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD"})
        if (git.Capture({"rev-parse", "-q", "--verify", ref}).exitCode == 0) return true;
    return InRebase(git);
}

fs::path ClashDir(const std::string& top) { return fs::path(top) / ".okay" / "clashes"; }
fs::path ClashReportPath(const std::string& top, const std::string& rel) { return ClashDir(top) / (rel + ".txt"); }

/// The files that have a clash report (paths relative to the top of the repository).
std::vector<std::string> ClashReports(const std::string& top) {
    std::vector<std::string> v;
    std::error_code ec;
    const fs::path dir = ClashDir(top);
    if (!fs::is_directory(dir, ec)) return v;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".txt") continue;
        std::string rel = RelTo(it->path().string(), dir.string());
        rel.resize(rel.size() - 4);
        v.push_back(rel);
    }
    std::sort(v.begin(), v.end());
    return v;
}

/// Reports from a merge that is no longer running (it was finished by hand or called off) are dropped.
void ClearStaleReports(const vc::Git& git, const std::string& top) {
    std::error_code ec;
    if (!fs::exists(ClashDir(top), ec) || MergeInProgress(git)) return;
    for (const vc::Change& c : git.Status()) if (c.state == vc::State::Conflict) return;
    fs::remove_all(ClashDir(top), ec);
}

std::vector<std::string> UnmergedFiles(const vc::Git& git) {
    std::vector<std::string> v;
    for (const vc::Change& c : git.Status()) if (c.state == vc::State::Conflict) v.push_back(c.path);
    return v;
}

const char* kResolveHint = "Settle each one with: okay vc resolve <file> --mine (keep yours), --theirs (take theirs),\n"
                           "or --as-is (keep the file as you fixed it in the editor). Then save.";

// ---- the scene merge in plain words -----------------------------------------------------------------------------

std::string Upgraded(const std::string& text) {
    const okay::SceneMigration::Result m = okay::SceneMigration::MigrateToCurrent(text);
    return m.ok && m.complete ? m.text : text;
}

std::string Shown(const std::string& v) {
    if (v.empty()) return "(empty)";
    return v.size() > 60 ? v.substr(0, 57) + "..." : v;
}

okay::SceneMerge::Options MergeOptions(okay::SceneMerge::Policy policy) {
    okay::SceneMerge::Options opt;
    opt.policy = policy;
    opt.validator = [](const std::string& text, std::string* error) {   // the merged text must load into a real scene
        okay::Scene probe("probe");
        return okay::SceneSerializer::Deserialize(probe, text, error);
    };
    return opt;
}

/// One clash in plain words. Git's "ours" is the person's side in a merge and the other side in a rebase.
std::string ClashSentence(const okay::SceneMerge::Conflict& c, bool rebase) {
    using K = okay::SceneMerge::Conflict::Kind;
    const std::string mine = rebase ? c.theirs : c.ours, other = rebase ? c.ours : c.theirs;
    const std::string who = !c.path.empty() ? c.path : !c.name.empty() ? c.name : std::string("Scene settings");
    const std::string part = c.record.empty() ? std::string("a part") : "the " + c.record + " part";
    std::string s;
    switch (c.kind) {
        case K::FieldChanged:
            s = who + ": " + (c.record.empty() ? std::string() : c.record + " ") + (c.field.empty() ? "value" : c.field) + " was " + Shown(c.base) +
                "; you made it " + Shown(mine) + " and they made it " + Shown(other) + ".";
            break;
        case K::RecordChanged: s = who + ": " + part + " was changed differently on both sides (yours: " + Shown(mine) + "; theirs: " + Shown(other) + ")."; break;
        case K::RecordDeletedAndEdited: s = who + ": one side removed " + part + " and the other changed it (yours: " + Shown(mine) + "; theirs: " + Shown(other) + ")."; break;
        case K::RecordAddedDifferently: s = who + ": both sides added " + part + ", in different ways (yours: " + Shown(mine) + "; theirs: " + Shown(other) + ")."; break;
        case K::ObjectDeletedAndEdited: s = who + ": one side deleted this object and the other changed it."; break;
        case K::RenamedDifferently: s = who + ": you renamed it " + Shown(mine) + " and they renamed it " + Shown(other) + "."; break;
        case K::ParentChanged: s = who + ": you and they moved it under different parents (yours: " + Shown(mine) + "; theirs: " + Shown(other) + ")."; break;
        case K::ParentDeleted: s = who + ": it was put under an object that the other side deleted."; break;
        case K::ParentCycle: s = who + ": the two moves together would put it inside itself."; break;
        case K::OrderChanged: s = who + ": you and they moved it to different places in the list."; break;
        case K::Invalid:
            return std::string("The two versions could not be combined into a scene that loads, so the file holds ") +
                   (rebase ? "their" : "your") + " whole version for now; " + (rebase ? "your" : "their") + " changes to it are NOT in it yet.";
    }
    if (c.resolution == "ours" || c.resolution == "theirs") {
        const bool keptMine = (c.resolution == "ours") != rebase;
        s += std::string(" For now the file has ") + (keptMine ? "yours" : "theirs") + ".";
    } else if (!c.resolution.empty()) s += " For now: " + c.resolution + ".";
    return s;
}

} // namespace

// ---- the drivers git calls --------------------------------------------------------------------------------------

int SceneMergeDriver(const std::string& basePath, const std::string& oursPath, const std::string& theirsPath,
                     const std::string& shownName, const Io& io) {
    bool okB = false, okO = false, okT = false;
    const std::string base = ReadFile(basePath, &okB), ours = ReadFile(oursPath, &okO), theirs = ReadFile(theirsPath, &okT);
    const std::string name = shownName.empty() ? fs::path(oursPath).filename().string() : shownName;
    if (!okO || !okT) { io.Warn(name + ": couldn't read the two versions to merge."); return 2; }
    const okay::SceneMerge::Result r =
        okay::SceneMerge::Merge(Upgraded(base), Upgraded(ours), Upgraded(theirs), MergeOptions(okay::SceneMerge::Policy::Mark));
    if (!r.ok) {
        // Not a scene the engine can read (or damaged): let git merge it as plain text, with its usual clash marks.
        io.Warn(name + ": not a scene the engine can read (" + r.error + "), so it was merged as plain text.");
        const RunResult t = Run({vc::Git::Program(), "merge-file", "-L", "yours", "-L", "base", "-L", "theirs", oursPath, basePath, theirsPath}, "", true);
        if (!t.started) return 2;
        return t.exitCode == 0 ? 0 : 1;
    }
    std::string err;
    if (!okayproj::WriteTextAtomic(oursPath, r.text, &err)) { io.Warn(name + ": couldn't write the merged scene: " + err); return 2; }
    if (r.Clean()) {
        io.Warn(name + ": merged both sets of changes with the engine's scene merge.");
        return 0;
    }
    // A real clash: git marks the file as not merged (exit 1), the file still loads, and a report lists every clash with
    // both values, so nothing of the other side is lost silently. `okay vc save` refuses until it is settled.
    const vc::Git here("");
    const bool rebase = InRebase(here);
    std::string t = name + ": " + std::to_string(r.conflicts.size()) + (r.conflicts.size() == 1 ? " change clashes" : " changes clash") +
                    " (you and they changed the same thing). The scene still loads.";
    for (const auto& c : r.conflicts) t += "\n  - " + ClashSentence(c, rebase);
    std::string top = here.TopLevel();
    if (top.empty()) { std::error_code ec; top = fs::current_path(ec).string(); }
    const fs::path report = ClashReportPath(top, name);
    std::error_code ec;
    fs::create_directories(report.parent_path(), ec);
    const std::string reportText = t + "\n\n" + kResolveHint + "\nfile: " + name + "\n";
    const bool wrote = okayproj::WriteTextAtomic(report.string(), reportText, &err);
    t += std::string("\n") + kResolveHint;
    if (wrote) t += "\nThis list is kept in " + RelTo(report.string(), top) + ".";
    io.Warn(t);
    return 1;
}

int SceneDiffDriver(const std::vector<std::string>& args, const Io& io) {
    std::string path, oldFile, newFile;
    if (args.size() >= 7) { path = args[0]; oldFile = args[1]; newFile = args[4]; }
    else if (args.size() == 2) { oldFile = args[0]; newFile = args[1]; path = newFile; }
    else { io.Warn("Usage: okay vc diff-driver <path> <old-file> <old-hex> <old-mode> <new-file> <new-hex> <new-mode>"); return 2; }
    auto load = [](const std::string& f) {
        if (f == "/dev/null" || f == "nul" || f == "NUL") return std::string();
        return ReadFile(f);
    };
    const std::string a = load(oldFile), b = load(newFile);
    const okay::SceneDiff::Result d = okay::SceneDiff::Compute(a, b);
    if (!d.ok) {   // not a readable scene: show the plain text difference
        const RunResult r = Run({vc::Git::Program(), "diff", "--no-index", "--no-ext-diff", "--", oldFile, newFile}, "", true);
        io.Say("scene " + path + " (not readable as a scene: " + d.error + "; plain text difference)\n" + r.out);
        return 0;
    }
    if (d.Empty()) { io.Say("scene " + path + ": no change to the scene (only how the file is laid out)"); return 0; }
    io.Say("scene " + path + "\n" + d.ToText());
    return 0;
}

namespace {

// ---- the commands -----------------------------------------------------------------------------------------------

/// The project's build output folder from its Build Settings (.okay/build_live.cfg), relative to the project.
std::string BuildOutputDir(const std::string& root) {
    oked::BuildProfile live;
    std::string selected;
    if (!oked::ParseBuildLive(ReadFile(oked::BuildLiveFile(root)), live, selected)) return "";
    std::string dir = live.outputDir;
    std::error_code ec;
    if (!dir.empty() && fs::path(dir).is_absolute()) {
        const std::string rel = RelTo(dir, root);
        dir = rel.rfind("..", 0) == 0 || fs::path(rel).is_absolute() ? std::string() : rel;
    }
    return dir;
}

int VcInit(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Flag("separate").Flag("outer");
    if (!ParseArgs(cl, args, "vc init", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc init")); return 0; }
    if (!NeedGit(io)) return 1;
    const std::string folder = VcFolder(project);
    if (FindProjectRoot(folder).empty()) io.Say("Note: this folder does not look like an OkaySpace project; starting version control anyway.");
    vc::Git git(folder);
    bool existed = git.IsRepo();
    if (existed && Canon(git.TopLevel()) != Canon(folder)) {
        // The project sits inside a bigger git folder: never change that one without being asked.
        if (cl.Has("separate")) existed = false;
        else if (!cl.Has("outer")) {
            io.Warn("This project is inside another Git folder (" + git.TopLevel() + "), so okay vc init would change that one.\n"
                    "Run okay vc init --separate to give the project its own version control, or --outer to set up the outer one.");
            return 1;
        }
    }
    if (!existed) {
        const RunResult r = git.Capture({"init", "-q", "--", folder});
        if (!r.started || r.exitCode != 0) { io.Warn("Couldn't start version control here." + GitSaid(r)); return 1; }
        git.Capture({"symbolic-ref", "HEAD", "refs/heads/main"});   // the first version line is called main (works on every git version)
    }
    int addedIgnore = 0, addedAttr = 0;
    std::string err;
    const fs::path ignore = fs::path(folder) / ".gitignore", attrs = fs::path(folder) / ".gitattributes";
    const std::string oldIgnore = ReadFile(ignore.string()), oldAttrs = ReadFile(attrs.string());
    const std::string newIgnore = vc::GitIgnoreText(oldIgnore, BuildOutputDir(folder), &addedIgnore);
    const std::string newAttrs = vc::GitAttributesText(oldAttrs, &addedAttr);
    if (addedIgnore && !okayproj::WriteTextAtomic(ignore.string(), newIgnore, &err)) { io.Warn("Couldn't write .gitignore: " + err); return 1; }
    if (addedAttr && !okayproj::WriteTextAtomic(attrs.string(), newAttrs, &err)) { io.Warn("Couldn't write .gitattributes: " + err); return 1; }
    // Tell git to merge and compare scenes and prefabs with okay (the engine's SceneMerge and SceneDiff).
    const std::string okayExe = StableOkayPath();
    if (okayExe.empty()) { io.Warn("Couldn't find the okay program's path, so Git can't be told to merge scenes with it."); return 1; }
    std::string why;
    if (!SetDrivers(git, okayExe, &why)) { io.Warn("Couldn't set up Git's scene merge." + why); return 1; }
    std::string t = existed ? "Version control was already on here; it is set up for OkaySpace now.\n" : "Started version control in " + folder + ".\n";
    t += addedIgnore ? "  .gitignore: " + std::to_string(addedIgnore) + " line(s) added (build output, caches, backups, your own editor files).\n"
                     : "  .gitignore already had everything.\n";
    t += addedAttr ? "  .gitattributes: " + std::to_string(addedAttr) + " line(s) added (scenes merge with the engine; pictures and sounds are binary).\n"
                   : "  .gitattributes already had everything.\n";
    t += "  Scenes and prefabs now merge and compare with the engine's own scene merge (" + okayExe + ").\n";
    if (!git.HasCommits()) t += "Make your first save with: okay vc save \"First version\"\n";
    io.Say(t);
    return 0;
}

int VcClone(const std::vector<std::string>& args, const std::string&, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "vc clone", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc clone")); return 0; }
    if (!NeedGit(io)) return 1;
    const std::string url = cl.Positional(0);
    if (url.empty()) { io.Warn("Say where the project is: okay vc clone <url or folder> [folder]"); return 2; }
    std::string dir = cl.Positional(1);
    if (dir.empty()) {
        std::string u = url;
        while (!u.empty() && (u.back() == '/' || u.back() == '\\')) u.pop_back();
        const std::size_t cut = u.find_last_of("/\\:");
        dir = cut == std::string::npos ? u : u.substr(cut + 1);
        if (dir.size() > 4 && dir.compare(dir.size() - 4, 4, ".git") == 0) dir.resize(dir.size() - 4);
        if (dir.empty()) { io.Warn("Say which folder to put it in: okay vc clone " + url + " <folder>"); return 2; }
    }
    std::error_code ec;
    const fs::path target = fs::absolute(dir, ec);
    if (fs::exists(target, ec) && !fs::is_empty(target, ec)) { io.Warn(target.string() + " already has files in it. Pick another folder."); return 1; }
    io.Say("Getting " + url + " into " + target.string() + "...");
    const RunResult r = Run({vc::Git::Program(), "clone", "-q", "--", url, target.string()}, "", false);
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't get the project. Check the address and that you may read it."); return 1; }
    return Main({"--project", target.string(), "vc", "init"}, io);
}

int VcStatus(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "vc status", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc status")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    const std::string top = git.TopLevel();
    ClearStaleReports(git, top);
    std::string t = vc::StatusText(git.Status(), git.Branch());
    const std::vector<std::string> reports = ClashReports(top);
    if (!reports.empty()) {
        t += "\nClashes to settle (" + std::to_string(reports.size()) + "):\n";
        for (const std::string& f : reports) t += "  " + f + "   (list: " + RelTo(ClashReportPath(top, f).string(), top) + ")\n";
        t += kResolveHint + std::string("\n");
    }
    if (!git.HasCommits()) t += "Nothing has been saved yet.\n";
    io.Say(t);
    return 0;
}

int VcWho(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Flag("global");
    if (!ParseArgs(cl, args, "vc who", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc who")); return 0; }
    if (!NeedGit(io)) return 1;
    vc::Git git(VcFolder(project));
    if (cl.Positionals().empty()) {
        const std::string name = Trim(git.Capture({"config", "user.name"}).out), email = Trim(git.Capture({"config", "user.email"}).out);
        io.Say(name.empty() ? std::string(kWhoHint) : "Your saves are signed " + name + (email.empty() ? "" : " <" + email + ">") + ".");
        return 0;
    }
    if (cl.Positionals().size() != 2) { io.Warn("Give your name and your email: okay vc who \"Your Name\" you@example.com"); return 2; }
    const bool global = cl.Has("global");
    if (!global && !git.IsRepo()) { io.Warn("This folder has no version control yet. Run okay vc init first, or add --global for every project."); return 1; }
    for (const auto& kv : {std::make_pair("user.name", cl.Positional(0)), std::make_pair("user.email", cl.Positional(1))}) {
        const RunResult r = git.Capture({"config", global ? "--global" : "--local", "--end-of-options", kv.first, kv.second});
        if (!r.started || r.exitCode != 0) { io.Warn("Couldn't store that." + GitSaid(r)); return 1; }
    }
    io.Say("Your saves are signed " + cl.Positional(0) + " <" + cl.Positional(1) + "> from now on" +
           (global ? " (every project on this computer)." : " (in this project; add --global for every project)."));
    return 0;
}

int VcSave(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Value("message", "TEXT").Short('m');
    if (!ParseArgs(cl, args, "vc save", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc save")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    std::string message = cl.Has("message") ? cl.Get("message") : JoinWords(cl.Positionals());
    if (Trim(message).empty()) { io.Warn("Say what you changed: okay vc save \"Added the second level\""); return 2; }
    const std::string top = git.TopLevel();
    ClearStaleReports(git, top);
    // Never save while a clash is not settled: git would take the file as it is and the other side's values in the
    // clash list would be lost without anyone choosing that.
    std::vector<std::string> open = UnmergedFiles(git);
    for (const std::string& f : ClashReports(top)) if (std::find(open.begin(), open.end(), f) == open.end()) open.push_back(f);
    if (!open.empty()) {
        std::string t = std::to_string(open.size()) + (open.size() == 1 ? " file has" : " files have") + " a clash that is not settled yet:";
        for (const std::string& f : open) t += "\n  " + f;
        io.Warn(t + "\n" + kResolveHint);
        return 1;
    }
    const std::vector<vc::Change> before = git.Status();
    if (before.empty() && !MergeInProgress(git)) { io.Say("Nothing has changed since the last save, so there is nothing to save."); return 0; }
    RunResult r = git.Capture({"add", "-A", "--", "."});
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't gather the changes." + GitSaid(r)); return 1; }
    r = git.Capture({"commit", "-q", "-m", message});
    if (!r.started || r.exitCode != 0) {
        if (IdentityProblem(r)) io.Warn(kWhoHint);
        else io.Warn("The save did not work." + GitSaid(r));
        return 1;
    }
    const std::string id = Trim(git.Capture({"rev-parse", "--short", "HEAD"}).out);
    io.Say("Saved " + std::to_string(before.size()) + (before.size() == 1 ? " change" : " changes") + " as " + id + ": " + message);
    return 0;
}

int VcResolve(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Flag("mine").Flag("theirs").Flag("as-is");
    if (!ParseArgs(cl, args, "vc resolve", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc resolve")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    const std::string file = cl.Positional(0);
    const int choices = (cl.Has("mine") ? 1 : 0) + (cl.Has("theirs") ? 1 : 0) + (cl.Has("as-is") ? 1 : 0);
    if (file.empty() || choices != 1) { io.Warn("Say the file and one choice: okay vc resolve <file> --mine | --theirs | --as-is"); return 2; }
    std::error_code ec;
    const fs::path abs = FileArg(file, project);
    const std::string top = git.TopLevel();
    const std::string rel = RelTo(abs.string(), top);
    if (rel.rfind("..", 0) == 0) { io.Warn(file + " is not inside this project."); return 1; }
    const fs::path report = ClashReportPath(top, rel);
    const bool unmerged = !Trim(git.Capture({"--literal-pathspecs", "ls-files", "-u", "--", abs.string()}).out).empty();
    if (!unmerged && !fs::exists(report, ec)) { io.Say(file + " has no clash to settle."); return 0; }
    std::string said;
    if (cl.Has("as-is")) {
        if (fs::is_regular_file(abs, ec) && HasClashMarks(ReadFile(abs.string()))) {
            io.Warn(file + " still has clash marks (<<<<<<< and >>>>>>>) in it. Remove them first, or choose --mine or --theirs.");
            return 1;
        }
        said = file + " is settled as it is now.";
    } else {
        if (!unmerged) {
            io.Warn("Git no longer has both versions of " + file + ". Open it, fix it, then run: okay vc resolve " + file + " --as-is");
            return 1;
        }
        // The index holds the three versions: :1: the common base, :2: git's "ours", :3: git's "theirs". In a rebase
        // "ours" is the line being rebased onto and "theirs" is the person's own save.
        const bool rebase = InRebase(git), wantMine = cl.Has("mine");
        const bool wantOursStage = wantMine != rebase;
        auto stage = [&](int n, bool& ok) {
            const RunResult r = git.Capture({"show", ":" + std::to_string(n) + ":" + rel});
            ok = r.started && r.exitCode == 0;
            return ok ? r.out : std::string();
        };
        bool ok1 = false, ok2 = false, ok3 = false;
        const std::string base = stage(1, ok1), ours = stage(2, ok2), theirs = stage(3, ok3);
        const std::string ext = abs.extension().string();
        std::string text;
        bool keepFile = true;
        if ((ext == ".okayscene" || ext == ".okayprefab") && ok2 && ok3) {
            // Merge again, settling every clash on the chosen side: the changes that did not clash stay.
            const okay::SceneMerge::Result m = okay::SceneMerge::Merge(
                Upgraded(base), Upgraded(ours), Upgraded(theirs),
                MergeOptions(wantOursStage ? okay::SceneMerge::Policy::PreferOurs : okay::SceneMerge::Policy::PreferTheirs));
            if (m.ok && !m.fellBack) {
                text = m.text;
                said = file + ": " + (wantMine ? "kept yours" : "took theirs") + " for each clash; every change that did not clash is kept too.";
            }
        }
        if (said.empty()) {   // not a scene, or the merge can't be made: the chosen side's whole file
            const bool okChosen = wantOursStage ? ok2 : ok3;
            text = wantOursStage ? ours : theirs;
            keepFile = okChosen;
            said = file + ": " + (wantMine ? "kept your whole version" : "took their whole version") + (okChosen ? "." : " (that side deleted it).");
        }
        std::string err;
        if (keepFile) {
            if (!okayproj::WriteTextAtomic(abs.string(), text, &err)) { io.Warn("Couldn't write " + file + ": " + err); return 1; }
        } else fs::remove(abs, ec);
    }
    const RunResult r = fs::exists(abs, ec) ? git.Capture({"--literal-pathspecs", "add", "--", abs.string()})
                                          : git.Capture({"--literal-pathspecs", "rm", "-q", "--cached", "--ignore-unmatch", "--", abs.string()});
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't mark " + file + " as settled." + GitSaid(r)); return 1; }
    fs::remove(report, ec);
    const std::vector<std::string> left = UnmergedFiles(git);
    io.Say(said + (left.empty() ? " Save with: okay vc save \"Merged\"" : " " + std::to_string(left.size()) + " more to settle (okay vc status)."));
    return 0;
}

int VcHistory(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Value("count", "N", "how many", "20").Short('n');
    if (!ParseArgs(cl, args, "vc history", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc history")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    if (!git.HasCommits()) { io.Say("Nothing has been saved yet."); return 0; }
    int n = 20;
    if (!cl.TryGetInt("count", 1, 100000, n)) { io.Warn("-n needs a number from 1 up."); return 2; }
    std::vector<std::string> a = {"--literal-pathspecs", "log", "-n", std::to_string(n), "--date=format:%Y-%m-%d %H:%M", "--format=%h%x1f%ad%x1f%an%x1f%s"};
    const std::string file = cl.Positional(0);
    if (!file.empty()) { a.push_back("--follow"); a.push_back("--"); a.push_back(FileArg(file, project).string()); }
    const RunResult r = git.Capture(a);
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't read the history." + GitSaid(r)); return 1; }
    std::string t;
    int count = 0;
    std::istringstream in(r.out);
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string> f;
        std::size_t at = 0;
        for (std::size_t s; (s = line.find('\x1f', at)) != std::string::npos; at = s + 1) f.push_back(line.substr(at, s - at));
        f.push_back(line.substr(at));
        if (f.size() < 4) continue;
        t += "  " + f[0] + "  " + f[1] + "  " + Pad(f[2], 16) + "  " + f[3] + "\n";
        ++count;
    }
    if (!count) { io.Say(file.empty() ? "Nothing has been saved yet." : file + " has never been saved."); return 0; }
    io.Say((file.empty() ? std::string("Saves, newest first:\n") : "Saves of " + file + ", newest first:\n") + t +
           "Put a file back with: okay vc undo <file> [--to ID]");
    return 0;
}

int VcUndo(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Value("to", "ID");
    if (!ParseArgs(cl, args, "vc undo", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc undo")); return 0; }
    const std::string folder = VcFolder(project);
    vc::Git git(folder);
    if (!NeedRepo(git, io)) return 1;
    const std::string file = cl.Positional(0);
    if (file.empty()) { io.Warn("Say which file to put back: okay vc undo Assets/Level.okayscene"); return 2; }
    if (!git.HasCommits()) { io.Warn("Nothing has been saved yet, so there is nothing to go back to."); return 1; }
    std::error_code ec;
    const fs::path abs = FileArg(file, project);
    if (fs::is_directory(abs, ec)) { io.Warn(file + " is a folder. Undo puts back one file at a time: okay vc undo <file>"); return 1; }
    const std::string given = cl.Get("to", "HEAD");
    const RunResult v = git.Capture({"rev-parse", "-q", "--verify", "--end-of-options", given + "^{commit}"});
    const std::string rev = Trim(v.out);
    if (v.exitCode != 0 || rev.empty()) { io.Warn("There is no save called \"" + given + "\". See them with: okay vc history " + file); return 1; }
    const std::string top = git.TopLevel();
    const std::string rel = RelTo(abs.string(), top);
    if (rel.rfind("..", 0) == 0) { io.Warn(file + " is not inside this project."); return 1; }
    const RunResult exists = git.Capture({"cat-file", "-e", rev + ":" + rel});
    if (exists.exitCode != 0) {
        io.Warn(given == "HEAD" ? file + " was never saved, so there is no saved copy to go back to."
                                : file + " is not in save " + given + ". See okay vc history " + file);
        return 1;
    }
    const RunResult same = git.Capture({"--literal-pathspecs", "diff", "--quiet", rev, "--", abs.string()});
    if (same.exitCode == 0 && fs::exists(abs, ec)) {
        io.Say(file + (given == "HEAD" ? " has no changes since the last save." : " is already the same as in save " + given + "."));
        return 0;
    }
    std::string backup;
    if (fs::is_regular_file(abs, ec)) {   // keep the current copy first, so undo never loses work
        // The name holds the file's whole path and a number, so two files called Level.okayscene in different folders,
        // or two undos in the same second, never overwrite each other's copy.
        std::string flat = rel;
        for (char& c : flat) if (c == '/' || c == '\\' || c == ':') c = '~';
        const fs::path dir = fs::path(folder) / ".okay" / "backups" / "undo";
        fs::create_directories(dir, ec);
        fs::path dst;
        for (int n = 1; n < 100000; ++n) {
            dst = dir / (flat + "." + Stamp() + (n > 1 ? "-" + std::to_string(n) : std::string()));
            if (!fs::exists(dst, ec)) break;
        }
        fs::copy_file(abs, dst, fs::copy_options::none, ec);
        if (ec) { io.Warn("Couldn't keep a copy of " + file + " first (" + ec.message() + "), so nothing was changed."); return 1; }
        backup = RelTo(dst.string(), folder);
    }
    const RunResult r = git.Capture({"--literal-pathspecs", "checkout", rev, "--", abs.string()});
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't put " + file + " back." + GitSaid(r)); return 1; }
    const std::string id = Trim(git.Capture({"rev-parse", "--short", rev}).out);
    io.Say(file + " is back to how it was in save " + id + "." + (backup.empty() ? "" : " Your copy from before is in " + backup + "."));
    return 0;
}

std::vector<std::string> Branches(const vc::Git& git) {
    std::vector<std::string> v;
    std::istringstream in(git.Capture({"branch", "--format=%(refname:short)"}).out);
    for (std::string l; std::getline(in, l);) { l = Trim(l); if (!l.empty()) v.push_back(l); }
    return v;
}

int VcBranch(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "vc branch", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc branch")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    const std::string cur = git.Branch();
    const std::string name = cl.Positional(0);
    if (name.empty()) {
        std::vector<std::string> list = Branches(git);
        if (list.empty() && !cur.empty()) list.push_back(cur);
        std::string t = "Version lines:\n";
        for (const std::string& b : list) t += (b == cur ? "  * " : "    ") + b + (b == cur ? "  (you are here)" : "") + "\n";
        io.Say(t + "Start a new one with: okay vc branch <name>");
        return 0;
    }
    if (!git.HasCommits()) { io.Warn("Save once first (okay vc save \"First version\"); a new version line starts from a save."); return 1; }
    if (git.Capture({"check-ref-format", "--branch", name}).exitCode != 0 || name[0] == '-') {
        io.Warn("\"" + name + "\" can't be a version line name. Use letters, numbers and dashes.");
        return 2;
    }
    const std::vector<std::string> list = Branches(git);
    if (std::find(list.begin(), list.end(), name) != list.end()) { io.Warn("There is already a version line called " + name + ". Move to it with: okay vc switch " + name); return 1; }
    const RunResult r = git.Capture({"checkout", "-q", "-b", name});
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't start the version line." + GitSaid(r)); return 1; }
    io.Say("Started the version line " + name + " from " + (cur.empty() ? std::string("here") : cur) + ", and you are on it now. Unsaved changes came with you.\n"
           "Go back with: okay vc switch " + (cur.empty() ? std::string("main") : cur));
    return 0;
}

int VcSwitch(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Flag("carry");
    if (!ParseArgs(cl, args, "vc switch", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc switch")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    const std::string name = cl.Positional(0);
    if (name.empty()) { io.Warn("Say which version line to move to. See them with: okay vc branch"); return 2; }
    const std::vector<std::string> list = Branches(git);
    if (std::find(list.begin(), list.end(), name) == list.end()) {
        std::string t = "There is no version line called " + name + ". There are:";
        for (const std::string& b : list) t += " " + b;
        io.Warn(t);
        return 1;
    }
    if (name == git.Branch()) { io.Say("You are already on " + name + "."); return 0; }
    const std::vector<vc::Change> changes = git.Status();
    if (!changes.empty() && !cl.Has("carry")) {
        io.Warn(std::to_string(changes.size()) + (changes.size() == 1 ? " change is" : " changes are") + " not saved yet. Save first (okay vc save \"message\"),\n"
                "or run okay vc switch " + name + " --carry to take the changes with you.");
        return 1;
    }
    const RunResult r = git.Capture({"checkout", "-q", name, "--"});
    if (!r.started || r.exitCode != 0) {
        io.Warn("Couldn't move to " + name + ". Your unsaved changes clash with files that differ there; save them first." + GitSaid(r));
        return 1;
    }
    io.Say("You are on " + name + " now. If the editor is open, open the scene again to see this version.");
    return 0;
}

int VcRemote(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "vc remote", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc remote")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    const std::string url = cl.Positional(0), have = git.Remote();
    if (url.empty()) {
        io.Say(have.empty() ? "This project has no online copy yet. Set one with: okay vc remote <url or folder>" : "The online copy is " + have + ".");
        return 0;
    }
    const RunResult r = git.Capture(have.empty() ? std::vector<std::string>{"remote", "add", "--end-of-options", "origin", url}
                                                 : std::vector<std::string>{"remote", "set-url", "--end-of-options", "origin", url});
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't set the online copy." + GitSaid(r)); return 1; }
    io.Say("The online copy is " + url + " now. Send your saves with: okay vc sync");
    return 0;
}

int VcSync(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "vc sync", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc sync")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    if (git.Remote().empty()) { io.Warn("This project has no online copy yet. Set one with: okay vc remote <url or folder>"); return 1; }
    const std::string branch = git.Branch();
    if (branch.empty()) { io.Warn("You are not on a version line, so there is nothing to sync. Move to one with okay vc switch main."); return 1; }
    const std::vector<vc::Change> changes = git.Status();
    if (!changes.empty() || MergeInProgress(git)) { io.Warn("Save your changes first (okay vc save \"message\"), then sync."); return 1; }
    io.Say("Getting new saves from the online copy...");
    RunResult r = git.Attached({"fetch", "-q", "origin"});
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't reach the online copy. Check the address (okay vc remote) and your connection."); return 1; }
    int got = 0;
    if (git.Capture({"rev-parse", "-q", "--verify", "refs/remotes/origin/" + branch}).exitCode == 0) {
        const std::string count = Trim(git.Capture({"rev-list", "--count", "HEAD..refs/remotes/origin/" + branch}).out);
        got = std::atoi(count.c_str());
        if (!git.HasCommits()) got = std::max(got, 1);
        if (got > 0) {
            r = git.Capture({"merge", "--no-edit", "-q", "refs/remotes/origin/" + branch});
            if (!r.started || r.exitCode != 0) {
                const std::vector<std::string> clash = UnmergedFiles(git);
                if (clash.empty()) {
                    io.Warn(IdentityProblem(r) ? std::string(kWhoHint) : "Couldn't take in the new saves." + GitSaid(r));
                    return 1;
                }
                // What the scene merge said about each clash (both values), then what to do.
                std::string t = "Someone else changed the same things as you.";
                const std::string said = Trim(r.err);
                if (!said.empty()) t += "\n" + said;
                t += "\nThese files clash:";
                for (const std::string& f : clash) t += "\n  " + f;
                io.Warn(t + "\n" + kResolveHint + "\nThen run okay vc save \"Merged\" and okay vc sync again.");
                return 1;
            }
        }
    }
    if (git.HasCommits()) {
        io.Say("Sending your saves...");
        r = git.Attached({"push", "-q", "-u", "origin", branch});
        if (!r.started || r.exitCode != 0) { io.Warn("Couldn't send your saves to the online copy. Check that you may write to it."); return 1; }
    }
    io.Say(got > 0 ? "Synced: took in " + std::to_string(got) + " new save(s) and sent yours." : "Synced: the online copy and this project match.");
    return 0;
}

} // namespace

namespace detail {

int VcMain(const std::vector<std::string>& rest, const std::string& project, const Io& io) {
    if (rest.empty()) { io.Say(HelpText("vc")); return 0; }
    const std::string sub = rest[0];
    const std::vector<std::string> more(rest.begin() + 1, rest.end());
    if (sub == "init") return VcInit(more, project, io);
    if (sub == "clone") return VcClone(more, project, io);
    if (sub == "status") return VcStatus(more, project, io);
    if (sub == "save") return VcSave(more, project, io);
    if (sub == "resolve") return VcResolve(more, project, io);
    if (sub == "history" || sub == "log") return VcHistory(more, project, io);
    if (sub == "undo") return VcUndo(more, project, io);
    if (sub == "branch") return VcBranch(more, project, io);
    if (sub == "switch") return VcSwitch(more, project, io);
    if (sub == "sync") return VcSync(more, project, io);
    if (sub == "remote") return VcRemote(more, project, io);
    if (sub == "who") return VcWho(more, project, io);
    // Hidden: what git itself calls (set up by okay vc init).
    if (sub == "merge-driver") {
        if (more.size() < 3) { io.Warn("Usage: okay vc merge-driver <base> <ours> <theirs> [path]"); return 2; }
        return SceneMergeDriver(more[0], more[1], more[2], more.size() > 3 ? more[3] : std::string(), io);
    }
    if (sub == "diff-driver") return SceneDiffDriver(more, io);
    io.Warn("There is no okay vc command called \"" + sub + "\".\n" + HelpText("vc"));
    return 2;
}

} // namespace detail
} // namespace okaycli
