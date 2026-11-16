#pragma once
// Test builds ("the dev channel"): builds of one branch that the coordinator publishes to the dev/ folder of the release bucket, so the owner
// can try unmerged work in about 30 minutes without waiting for the pull-request checks and a release. Header-only and free of SDL, ImGui,
// network and engine code (like shared/ProjectVersion.hpp, which it builds on), so the launcher, the editor and the tests share exactly
// the same rules. Docs: docs/release_channels.md ("Test builds"), docs/updater.md.
//
// THE NUMBER. A normal release is 2.<build>.<patch> (three numbers). A test build has a FOURTH number, the workflow run number:
//     2.876.0.17  =  the 17th test build, made on top of release 2.876.0
// Why a fourth number and not a "-dev" suffix: shared/ProjectVersion.hpp ignores a suffix after '-', so "2.876.0-dev17" would compare EQUAL to
// the release 2.876.0 and a launcher could not tell them apart. A fourth number is ordered by the parser that already exists:
//     2.876.0   <   2.876.0.17   <   2.876.0.18   <   2.876.1   <   2.877.0
// A test build sorts just above the release it was made on (so a launcher on that release is offered it) and below every later release (so the
// next real release always replaces it). The fourth number is never 0 (2.876.0.0 would equal 2.876.0). Test builds are never numbered by the
// stable publisher: scripts/publish-supabase.sh and scripts/versions_index.py refuse a version with four numbers, and the launcher's stable
// reader (ParseIndex, ParseVersionTxt) skips one, so a test version can never become "latest".
//
// WHAT THE LAUNCHER DOES (launcher/main.cpp): nothing at all unless the person switched "Get test builds" on (launcher.cfg: test_builds=1, off by
// default). When it is on, the update check also reads <bucket>/dev/versions.json (signed like the real one) and ChooseTarget picks the newer of
// "the newest normal release" and "the newest test build". Going back (ChooseTarget with goBack) installs the newest normal release over a test
// build, the one deliberate downgrade; it uses the same checksum, stage, swap, self-check and rollback rules as every other update.
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "ProjectVersion.hpp"

namespace okay::testbuild {
namespace fs = std::filesystem;

inline constexpr const char* kArea = "dev";                       ///< the folder of the release bucket that holds test builds
inline constexpr const char* kIndexName = "versions.json";        ///< dev/versions.json: the signed list (the only file the launcher trusts)
inline constexpr const char* kHeadName = "DEV.txt";               ///< dev/DEV.txt: the newest test version, uploaded last (for people and scripts)
inline constexpr const char* kNotesName = "NOTES-DEV.txt";        ///< dev/NOTES-DEV.txt: the newest test build's notes (the launcher's "what's new")
inline constexpr const char* kMarkerName = "okay_testbuild.txt";  ///< next to the launcher: which test build is installed
inline constexpr std::size_t kMaxBranchChars = 80;
inline constexpr std::size_t kMaxNoteChars = 140;

// ---- the number ----------------------------------------------------------------------------------------------------------------

/// How many numbers a version has ("2.876.0" -> 3, "2.876.0.17" -> 4); 0 when `text` is not a version.
inline std::size_t NumberCount(std::string_view text) {
    const okayver::Version v = okayver::ParseVersion(text);
    return v.valid ? v.parts.size() : 0;
}
/// A normal release: one to three numbers (2.876.0). Text that is not a version is not one.
inline bool IsReleaseShaped(std::string_view text) {
    const std::size_t n = NumberCount(text);
    return n >= 1 && n <= 3;
}
/// A test build: exactly four numbers, the fourth at least 1 (2.876.0.17).
inline bool IsTestVersion(std::string_view text) {
    const okayver::Version v = okayver::ParseVersion(text);
    return v.valid && v.parts.size() == 4 && v.parts[3] >= 1;
}
/// The release a test build was made on: "2.876.0.17" -> "2.876.0"; "" when `text` is not a test version.
inline std::string BaseRelease(std::string_view text) {
    if (!IsTestVersion(text)) return {};
    const okayver::Version v = okayver::ParseVersion(text);
    return std::to_string(v.parts[0]) + "." + std::to_string(v.parts[1]) + "." + std::to_string(v.parts[2]);
}
/// The fourth number ("2.876.0.17" -> 17); 0 when `text` is not a test version.
inline std::uint64_t Serial(std::string_view text) {
    return IsTestVersion(text) ? okayver::ParseVersion(text).parts[3] : 0;
}
/// "2.876.0" + 17 -> "2.876.0.17". "" unless `base` is exactly three numbers and `serial` is at least 1.
inline std::string MakeVersion(std::string_view base, std::uint64_t serial) {
    const okayver::Version b = okayver::ParseVersion(base);
    if (!b.valid || b.parts.size() != 3 || serial < 1) return {};
    return okayver::FormatVersion(b) + "." + std::to_string(serial);
}
/// " (test build)" for a test version, "" for anything else: what follows a version where it is shown ("v2.876.0.17 (test build)").
inline std::string Suffix(std::string_view version) { return IsTestVersion(version) ? " (test build)" : ""; }
/// "v2.876.0.17 (test build)", or "v2.876.0" for a release; the text itself when it is not a version.
inline std::string DisplayVersion(std::string_view version) { return okayver::DisplayVersion(version) + Suffix(version); }

// ---- the words ----------------------------------------------------------------------------------------------------------------
//
// The branch, the commit and the note come from whoever starts the build, so they are cleaned before they are stored (the publisher) and again
// before they are shown (the launcher). They are only ever drawn as text: never put into a path, a command or a URL.

/// One line of plain printable ASCII: other characters are dropped, runs of spaces become one, the ends are trimmed, at most `maxChars` long.
inline std::string CleanLine(std::string_view s, std::size_t maxChars = kMaxNoteChars) {
    std::string out;
    bool space = false;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t') { space = !out.empty(); continue; }
        if (c < 0x21 || c > 0x7E) continue;
        if (out.size() + (space ? 2 : 1) > maxChars) break;
        if (space) { out += ' '; space = false; }
        out += static_cast<char>(c);
    }
    return out;
}
/// A git branch (or tag) name as a label: letters, digits and . _ / + - only, at most 80 characters, no "..", no leading '/', '-' or '.', no
/// trailing '/'. "" when it is not.
inline std::string CleanBranch(std::string_view s) {
    if (s.empty() || s.size() > kMaxBranchChars) return {};
    for (unsigned char c : s)
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '/' || c == '+' || c == '-')) return {};
    if (s.find("..") != std::string_view::npos) return {};
    if (s.front() == '/' || s.front() == '-' || s.front() == '.' || s.back() == '/') return {};
    return std::string(s);
}
/// A commit id: lowercase hex, 7 to 40 digits (upper case is folded). "" when it is not.
inline std::string CleanSha(std::string_view s) {
    if (s.size() < 7 || s.size() > 40) return {};
    std::string out;
    for (unsigned char c : s) {
        if (c >= 'A' && c <= 'F') c = static_cast<unsigned char>(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return {};
        out += static_cast<char>(c);
    }
    return out;
}
/// The seven digits people say ("abc1234"); "" when `sha` is not a commit id.
inline std::string ShortSha(std::string_view sha) {
    const std::string c = CleanSha(sha);
    return c.empty() ? std::string() : c.substr(0, 7);
}
/// The branch as people say it: the agents' "claude/" and "claude2/" folders are dropped ("claude/walk-vs-run" -> "walk-vs-run").
inline std::string DisplayBranch(std::string_view branch) {
    for (const std::string_view prefix : {std::string_view("claude2/"), std::string_view("claude/")})
        if (branch.size() > prefix.size() && branch.compare(0, prefix.size(), prefix) == 0) return std::string(branch.substr(prefix.size()));
    return std::string(branch);
}
/// "Test build of walk-vs-run (abc1234)". Missing parts are left out: no commit gives "Test build of walk-vs-run", neither gives "Test build".
inline std::string Title(std::string_view branch, std::string_view sha) {
    const std::string b = DisplayBranch(CleanBranch(branch)), s = ShortSha(sha);
    std::string out = "Test build";
    if (!b.empty()) out += " of " + b;
    if (!s.empty()) out += " (" + s + ")";
    return out;
}

// ---- what to install ----------------------------------------------------------------------------------------------------------

/// What the update check should install. `version` is normalized; `note` says in one plain sentence why there is nothing to do.
struct Choice {
    enum Kind {
        None,         ///< nothing to install (`note` says why)
        Stable,       ///< the newest normal release (today's behaviour)
        TestBuild,    ///< the newest test build
        GoBack,       ///< the newest normal release, installed over a test build that is newer than it (the one deliberate downgrade)
    } kind = None;
    std::string version;
    std::string note;
    bool failed = false;    ///< `None` because something could not be found out (offline, no answer), not because there is nothing to do
};

/// Which version the update check should work towards.
///   local          the installed version
///   stableHead     the newest normal release the server announces ("" = not known); a version with four numbers is not one
///   testHead       the newest installable test build ("" = none or not asked for); a version without four numbers is not one
///   testBuildsOn   "Get test builds" is on
///   onLts          the long-term-support channel is chosen: test builds are not offered there
///   goBack         the person asked to leave a test build
/// With the switch off and no go-back, the answer is the stable head and nothing else, exactly as before test builds existed. With it on, the
/// newer of the two heads wins. The caller still installs only what is newer than `local` (a go-back is the exception it makes itself).
inline Choice ChooseTarget(std::string_view local, std::string_view stableHead, std::string_view testHead, bool testBuildsOn, bool onLts,
                           bool goBack) {
    Choice c;
    const std::string stable = IsReleaseShaped(stableHead) ? okayver::NormalizeVersion(stableHead) : std::string();
    const std::string test = (testBuildsOn && !onLts && IsTestVersion(testHead)) ? okayver::NormalizeVersion(testHead) : std::string();
    if (goBack) {
        if (stable.empty()) { c.note = "Couldn't find out which normal release is the newest, so nothing was changed."; c.failed = true; return c; }
        if (!IsTestVersion(local)) { c.note = "You're already on a normal release (" + okayver::DisplayVersion(local) + "), so there is nothing to go back to."; return c; }
        c.kind = okayver::CompareVersions(stable, local) > 0 ? Choice::Stable : Choice::GoBack;   // a newer release is an ordinary update
        c.version = stable;
        return c;
    }
    if (stable.empty() && test.empty()) { c.note = "Couldn't find out which version is the newest."; c.failed = true; return c; }
    if (!test.empty() && (stable.empty() || okayver::CompareVersions(test, stable) > 0)) { c.kind = Choice::TestBuild; c.version = test; return c; }
    c.kind = Choice::Stable;
    c.version = stable;
    return c;
}

// ---- which test build is installed -------------------------------------------------------------------------------------------
//
// After a test build is installed the launcher writes okay_testbuild.txt next to itself, so About can say "Test build of walk-vs-run (abc1234)".
// It is a hint for the person, never a decision: nothing is installed, replaced or trusted because of it, and it counts only while its version
// is the version that is running.

struct Marker {
    std::string version, branch, sha, note;
    bool Valid() const { return IsTestVersion(version); }
};

inline std::string MarkerText(const Marker& m) {
    std::string t = "okayspace test build 1\n";
    t += "version=" + okayver::NormalizeVersion(m.version) + "\n";
    t += "branch=" + CleanBranch(m.branch) + "\n";
    t += "sha=" + CleanSha(m.sha) + "\n";
    t += "note=" + CleanLine(m.note) + "\n";
    return t;
}

inline Marker ParseMarker(std::string_view text) {
    Marker m;
    if (text.size() > 2048) return m;
    std::istringstream in{std::string(text)};
    std::string line;
    if (!std::getline(in, line)) return m;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "okayspace test build 1") return m;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string_view lv(line);
        const std::size_t eq = lv.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view k = lv.substr(0, eq), v = lv.substr(eq + 1, lv.size() - eq - 1);   // (views: nothing is copied)
        if (k == "version") m.version = okayver::NormalizeVersion(v);
        else if (k == "branch") m.branch = CleanBranch(v);
        else if (k == "sha") m.sha = CleanSha(v);
        else if (k == "note") m.note = CleanLine(v);
    }
    if (!m.Valid()) return Marker{};
    return m;
}

inline fs::path MarkerPath(const fs::path& launcherDir) { return launcherDir / kMarkerName; }

/// Write the marker (through a temp file and a rename). False when it could not be written; the update that asked for it does not depend on it.
inline bool WriteMarker(const fs::path& launcherDir, const Marker& m) {
    if (!m.Valid()) return false;
    const fs::path file = MarkerPath(launcherDir);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o << MarkerText(m);
        o.flush();
        if (!o) { std::error_code ec; fs::remove(tmp, ec); return false; }
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}
inline Marker ReadMarker(const fs::path& launcherDir) {
    std::error_code ec;
    const fs::path file = MarkerPath(launcherDir);
    if (!fs::is_regular_file(file, ec)) return Marker{};
    std::ifstream in(file, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ParseMarker(ss.str());
}
inline void RemoveMarker(const fs::path& launcherDir) {
    std::error_code ec;
    fs::remove(MarkerPath(launcherDir), ec);
    fs::path tmp = MarkerPath(launcherDir);
    tmp += ".tmp";
    fs::remove(tmp, ec);
}

/// What About says about the running program: "" for a normal release (nothing to add), otherwise "Test build of walk-vs-run (abc1234)" when the
/// marker is for this very version, else "Test build" (the version has four numbers, so it is one, but nobody recorded which).
inline std::string RunningTitle(std::string_view runningVersion, const Marker& marker) {
    if (!IsTestVersion(runningVersion)) return {};
    if (marker.Valid() && okayver::CompareVersions(marker.version, runningVersion) == 0) return Title(marker.branch, marker.sha);
    return "Test build";
}

}  // namespace okay::testbuild
