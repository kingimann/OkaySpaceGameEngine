#pragma once
// okay CLI: version control through the git program the person has installed. Git.cpp is the only file that runs git
// (through Process.hpp, the one process seam); no git library is linked. Nothing here is GitHub-specific: a remote is
// any URL or folder git understands.
//
// The text helpers (the .gitignore and .gitattributes lines, reading `git status`, the merge and diff driver settings)
// have no side effects, so the tests check them directly. Written from the git documentation (git-status porcelain
// format, gitattributes merge and diff drivers, git-config). Our own code.
#include <string>
#include <vector>

#include "Process.hpp"

namespace okaycli::vc {

// ---- what a project keeps out of git, and how git treats its files ------------------------------------------------

/// The lines `okay vc init` puts in .gitignore: build output, caches, backups, autosaves, logs and per-person editor
/// files. Includes the editor's Build Settings list (editor/BuildSettingsModel.hpp IgnoreLines).
std::vector<std::string> IgnoreLines();
/// The lines `okay vc init` puts in .gitattributes: scenes and prefabs merge and diff with the engine's own scene merge
/// and diff (merge=okayscene diff=okayscene), pictures, sounds, models and fonts are binary.
std::vector<std::string> AttributeLines();
/// The new text of .gitignore / .gitattributes: `existing` plus the wanted lines it does not have yet, under a comment.
/// Nothing is removed or reordered (the editor's Build Settings merge, BuildSettingsModel.hpp MergeIgnore).
std::string GitIgnoreText(const std::string& existing, int* added = nullptr);
std::string GitAttributesText(const std::string& existing, int* added = nullptr);

/// The command git runs for a scene merge: "<okay>" vc merge-driver %O %A %B %P (forward slashes, quoted for git's shell).
std::string MergeDriverCommand(const std::string& okayExe);
/// The command git runs to show a scene diff: "<okay>" vc diff-driver
std::string DiffDriverCommand(const std::string& okayExe);

// ---- reading `git status` ----------------------------------------------------------------------------------------

enum class Group { Scenes, Scripts, Assets, Settings };
const char* GroupName(Group g);   ///< "Scenes and prefabs", "Scripts", "Assets", "Project settings"
Group GroupOf(const std::string& path);

enum class State { New, Changed, Deleted, Renamed, Conflict };
const char* StateWord(State s);   ///< "new", "changed", "deleted", "renamed", "clash" (a merge conflict)

struct Change {
    std::string path;   ///< the file, relative to the project (forward slashes)
    std::string from;   ///< the old name of a renamed file
    State state = State::Changed;
    Group group = Group::Assets;
};

/// Read `git status --porcelain=v1 -z` output.
std::vector<Change> ParseStatus(const std::string& porcelainZ);
/// The plain-words status: the version line (branch) and the changes grouped by scenes, scripts, assets and settings.
std::string StatusText(const std::vector<Change>& changes, const std::string& branch);

// ---- running git -------------------------------------------------------------------------------------------------

/// git in one project folder. Every call goes through okaycli::Run.
class Git {
public:
    explicit Git(std::string folder) : m_dir(std::move(folder)) {}
    const std::string& Folder() const { return m_dir; }
    /// git on PATH (or OKAY_GIT) can start.
    static bool Available();
    /// The program that is run ("git", or OKAY_GIT).
    static std::string Program();
    /// "git version 2.43.0" or "".
    static std::string Version();
    /// git <args>, output captured.
    RunResult Capture(const std::vector<std::string>& args) const;
    /// git <args>, sharing the terminal (for fetch and push, which may ask for a password).
    RunResult Attached(const std::vector<std::string>& args) const;
    /// The folder is inside a git work tree.
    bool IsRepo() const;
    /// The work tree's top folder ("" when not a repo).
    std::string TopLevel() const;
    /// The current branch name ("" when detached or not a repo).
    std::string Branch() const;
    /// At least one save (commit) exists.
    bool HasCommits() const;
    /// The changes not saved yet.
    std::vector<Change> Status() const;
    /// The URL of the remote called origin ("" when none).
    std::string Remote() const;
private:
    std::string m_dir;
};

} // namespace okaycli::vc
