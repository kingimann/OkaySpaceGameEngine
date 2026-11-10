#pragma once
// Which engine version a project was made with, and what to do when the version of the editor in front of the
// person differs from it. Header-only and free of engine, ImGui and SDL code, so the editor, the launcher and the
// tests share exactly the same rules (the launcher does not link the engine).
//
//   ParseVersion / FormatVersion / CompareVersions   "2.855.0", "v2.855", "dev", "" and garbage, never throwing
//   LineOf / SameLine / LineLabel                    the release line: the first two numbers ("2.850"); a patch release
//                                                    (2.850.1) stays on its line
//   Decide(projectVersion, runningVersion)           Same, SamePatchLine, Older, Newer, Unknown or DevBuild
//   ResolveKeepTarget                                which installed editor "Keep" starts: the newest of the project's line
//   ReadProjectStamp / ReadProjectVersion            the `engine_version` line of <project>/project.okayproj
//   WriteProjectVersion                              change only that line (every other byte of the file stays)
//   SetStampInText / FindStampInText                 the same on the text, for callers that write the file whole
//   KeepUnknownLines                                 lines of an existing file a writer does not know, to carry over
//   Versions folder helpers                          where the launcher keeps editors of other versions
//
// The stamp is the engine version that LAST WROTE the project: `engine_version=2.855.0`, one line in
// project.okayproj. Editors that do not know the key ignore it (every reader of that file skips unknown lines), so
// a stamped project still opens in an older editor. Nothing here prints or throws: functions return text or flags.
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include "ProjectCreate.hpp"

namespace okayver {
namespace fs = std::filesystem;

/// The key in project.okayproj.
inline const char* StampKey() { return "engine_version"; }
/// The command-line flag that says "the person already chose this version for this project: do not ask".
inline const char* KeepVersionFlag() { return "--keep-version"; }
/// The folder, beside the launcher, that holds editors of other versions: <launcher dir>/Versions/<ver>/Tools/<editor>.
inline const char* VersionsDirName() { return "Versions"; }
/// The folder the editor lives in inside an install (and inside every Versions/<ver>/ copy).
inline const char* ToolsDirName() { return "Tools"; }
/// The editor's file name on this platform.
inline const char* EditorExeName() {
#if defined(_WIN32)
    return "OkayEngine.exe";
#else
    return "OkayEngine";
#endif
}

// ---- versions ------------------------------------------------------------------------------------------------------

/// A parsed version. `parts` are the numeric components ("2.855.0" -> 2, 855, 0). A component too big for 64 bits
/// is clamped to the biggest value, so a hostile or corrupt file can't overflow anything.
struct Version {
    bool valid = false;               ///< at least one numeric component was read
    bool dev = false;                 ///< the text was "dev" (an unversioned development build)
    std::vector<std::uint64_t> parts;
};

constexpr std::size_t kMaxVersionChars = 64;   ///< longer text is not a version
constexpr std::size_t kMaxVersionParts = 16;

inline std::string_view TrimSpace(std::string_view s) {
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; };
    while (!s.empty() && ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && ws(s.back())) s.remove_suffix(1);
    return s;
}

/// Read a version out of text: "2.855.0", "v2.855.0", "2.855" (missing parts are zero), " 2.855.0\r\n". A suffix after
/// the last number that starts with '-', '+', '_' or a space is allowed and ignored ("2.855.0-rc1"). "dev" gives
/// `dev = true` and no numbers. Empty text, "garbage", "2.x", "2.855.0banana" and anything longer than 64 characters
/// give `valid = false`.
inline Version ParseVersion(std::string_view text) {
    Version v;
    std::string_view s = TrimSpace(text);
    if (s.empty() || s.size() > kMaxVersionChars) return v;
    if (s.size() >= 3 && (s[0] == 'd' || s[0] == 'D') && (s[1] == 'e' || s[1] == 'E') && (s[2] == 'v' || s[2] == 'V') &&
        (s.size() == 3 || !std::isalnum(static_cast<unsigned char>(s[3])))) {
        v.dev = true;
        return v;
    }
    if (s[0] == 'v' || s[0] == 'V') s.remove_prefix(1);
    std::size_t i = 0;
    std::vector<std::uint64_t> parts;
    while (i < s.size() && parts.size() < kMaxVersionParts) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) break;
        std::uint64_t n = 0;
        bool clamped = false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            const unsigned d = static_cast<unsigned>(s[i] - '0');
            if (!clamped) {
                if (n > (UINT64_MAX - d) / 10) { n = UINT64_MAX; clamped = true; }
                else n = n * 10 + d;
            }
            ++i;
        }
        parts.push_back(n);
        if (i + 1 < s.size() && s[i] == '.' && std::isdigit(static_cast<unsigned char>(s[i + 1]))) { ++i; continue; }
        break;
    }
    if (parts.empty()) return v;
    if (i < s.size()) {                                              // what follows the last number must be a suffix
        const char c = s[i];
        if (c != '-' && c != '+' && c != '_' && c != ' ') return v;
    }
    v.valid = true;
    v.parts = std::move(parts);
    return v;
}

/// The canonical text of a version: at least three numbers ("2.855" -> "2.855.0"), no "v". Empty when `v` is not valid.
inline std::string FormatVersion(const Version& v) {
    if (!v.valid || v.parts.empty()) return {};
    std::string out;
    const std::size_t n = std::max<std::size_t>(3, v.parts.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (i) out += '.';
        out += std::to_string(i < v.parts.size() ? v.parts[i] : 0ull);
    }
    return out;
}
/// ParseVersion + FormatVersion: "v2.855" -> "2.855.0"; "" for text that is not a version (and for "dev").
inline std::string NormalizeVersion(std::string_view text) { return FormatVersion(ParseVersion(text)); }
/// True for the unversioned development build ("dev").
inline bool IsDevVersion(std::string_view text) { return ParseVersion(text).dev; }
/// "v2.855.0" for a version, the text itself otherwise (what the dialogs print).
inline std::string DisplayVersion(std::string_view text) {
    const std::string n = NormalizeVersion(text);
    return n.empty() ? std::string(TrimSpace(text)) : "v" + n;
}

/// -1, 0 or 1 for a < b, a == b, a > b. Numbers compare one component at a time, a missing component is zero
/// ("2.855" == "2.855.0", "2.9" < "2.855"), and a leading "v" is ignored. Text that is not a version ("dev", "",
/// garbage) is older than every real version and equal to other such text, so an unversioned build always looks
/// out of date next to a release and two unknowns never look different.
inline int CompareVersions(std::string_view a, std::string_view b) {
    const Version x = ParseVersion(a), y = ParseVersion(b);
    if (!x.valid || !y.valid) return x.valid == y.valid ? 0 : (x.valid ? 1 : -1);
    const std::size_t n = std::max(x.parts.size(), y.parts.size());
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t p = i < x.parts.size() ? x.parts[i] : 0, q = i < y.parts.size() ? y.parts[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

// ---- release lines -------------------------------------------------------------------------------------------------
//
// Releases are numbered 2.<build>.<patch>. The first two numbers are the LINE; a long-term line only gets fixes, as
// patch releases of the same build number (2.850.1, 2.850.2). A patch never changes a file format, so two versions of
// one line can open each other's projects with nothing to ask.

/// The line of a version: its first two numbers ("2.850.1" -> "2.850", "v3" -> "3.0"); "" when `text` is not a version.
inline std::string LineOf(std::string_view text) {
    const Version v = ParseVersion(text);
    if (!v.valid) return {};
    return std::to_string(v.parts[0]) + "." + std::to_string(v.parts.size() > 1 ? v.parts[1] : 0ull);
}
/// True when both are versions of the same line ("2.850.0" and "2.850.1"; also true for two spellings of one version).
inline bool SameLine(std::string_view a, std::string_view b) {
    const std::string x = LineOf(a);
    return !x.empty() && x == LineOf(b);
}
/// How a dialog names a line: "v2.850.x"; "" when `text` is not a version.
inline std::string LineLabel(std::string_view text) {
    const std::string l = LineOf(text);
    return l.empty() ? std::string() : "v" + l + ".x";
}
/// The installed editor that "Keep" should start for a project last written by `projectVersion`: the NEWEST installed
/// version of the same line (a 2.850.0 project keeps 2.850.1 when that is what is installed), normalized; "" when
/// no version of that line is installed. Pass the versions that have an editor under Versions/ (InstalledVersions).
inline std::string ResolveKeepTarget(std::string_view projectVersion, const std::vector<std::string>& installedVersions) {
    std::string best;
    for (const std::string& v : installedVersions) {
        if (!SameLine(v, projectVersion)) continue;
        if (best.empty() || CompareVersions(v, best) > 0) best = NormalizeVersion(v);
    }
    return best;
}

// ---- the decision --------------------------------------------------------------------------------------------------

enum class Decision {
    Same,      ///< the project was last written by this version: nothing to ask
    SamePatchLine,   ///< a different patch of the running editor's line (2.850.0 project, 2.850.1 editor, or the reverse): nothing to ask; the next save stamps the running version
    Older,     ///< the project was last written by an older version than the running editor: offer to upgrade it (or keep its version)
    Newer,     ///< the project was last written by a newer version than the running editor: this editor may drop what it does not know
    Unknown,   ///< no usable stamp: a project made before versions were recorded
    DevBuild,  ///< the running editor is an unversioned development build: never ask
};

inline const char* DecisionName(Decision d) {
    switch (d) {
        case Decision::Same: return "Same";
        case Decision::SamePatchLine: return "SamePatchLine";
        case Decision::Older: return "Older";
        case Decision::Newer: return "Newer";
        case Decision::Unknown: return "Unknown";
        case Decision::DevBuild: return "DevBuild";
    }
    return "?";
}

/// What the running editor should do about a project stamped `projectVersion`. A development build (or a running
/// version that is not a version at all) never asks. Otherwise: no usable stamp is Unknown, equal is Same, another
/// patch of the same line is SamePatchLine, and the rest is Older or Newer from the PROJECT's point of view.
inline Decision Decide(std::string_view projectVersion, std::string_view runningVersion) {
    const Version run = ParseVersion(runningVersion);
    if (!run.valid) return Decision::DevBuild;
    const Version proj = ParseVersion(projectVersion);
    if (!proj.valid) return Decision::Unknown;
    const int c = CompareVersions(projectVersion, runningVersion);
    if (c == 0) return Decision::Same;
    if (SameLine(projectVersion, runningVersion)) return Decision::SamePatchLine;
    return c < 0 ? Decision::Older : Decision::Newer;
}

// ---- the stamp in text -----------------------------------------------------------------------------------------------

namespace detail {
/// One line of a text: [begin, end) without its line ending, and `next`, the start of the following line.
struct LineSpan { std::size_t begin, end, next; };
inline LineSpan NextLine(const std::string& t, std::size_t pos) {
    std::size_t nl = t.find('\n', pos);
    LineSpan s{pos, nl == std::string::npos ? t.size() : nl, nl == std::string::npos ? t.size() : nl + 1};
    if (s.end > s.begin && t[s.end - 1] == '\r') --s.end;
    return s;
}
/// The key of a `key=value` line (spaces around the key trimmed, a UTF-8 BOM at the very start skipped), the value
/// (trimmed) through `value`. False for a line without '='.
inline bool SplitKeyValue(const std::string& t, LineSpan s, bool first, std::string* key, std::string* value) {
    std::size_t b = s.begin;
    if (first && s.end - b >= 3 && static_cast<unsigned char>(t[b]) == 0xEF && static_cast<unsigned char>(t[b + 1]) == 0xBB &&
        static_cast<unsigned char>(t[b + 2]) == 0xBF) b += 3;
    const std::size_t eq = t.find('=', b);
    if (eq == std::string::npos || eq >= s.end) return false;
    if (key) *key = std::string(TrimSpace(std::string_view(t).substr(b, eq - b)));
    if (value) *value = std::string(TrimSpace(std::string_view(t).substr(eq + 1, s.end - eq - 1)));
    return true;
}
}  // namespace detail

/// The raw value of the `engine_version` line in `text` (the last one if there are several). False when there is none.
inline bool FindStampInText(const std::string& text, std::string* raw = nullptr) {
    bool found = false;
    std::string k, v;
    for (std::size_t pos = 0, n = 0; pos < text.size(); ++n) {
        const detail::LineSpan s = detail::NextLine(text, pos);
        if (detail::SplitKeyValue(text, s, n == 0, &k, &v) && k == StampKey()) { found = true; if (raw) *raw = v; }
        pos = s.next;
    }
    return found;
}

/// `text` with its `engine_version` line set to `version` and every other byte as it was: other lines, their order,
/// their line endings (LF or CRLF), a BOM, a missing final newline. The line sits where the old one was; a text
/// without one gets it appended (a missing final newline is added first, in the file's own line-ending style); a
/// second `engine_version` line is dropped. `version` is written as given: pass NormalizeVersion's result.
inline std::string SetStampInText(const std::string& text, const std::string& version) {
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string out;
    out.reserve(text.size() + 32);
    bool placed = false;
    std::string k;
    for (std::size_t pos = 0, n = 0; pos < text.size(); ++n) {
        const detail::LineSpan s = detail::NextLine(text, pos);
        const bool ours = detail::SplitKeyValue(text, s, n == 0, &k, nullptr) && k == StampKey();
        if (!ours) { out.append(text, s.begin, s.next - s.begin); }
        else if (!placed) {
            placed = true;
            if (n == 0 && s.end - s.begin >= 3 && static_cast<unsigned char>(text[s.begin]) == 0xEF &&
                static_cast<unsigned char>(text[s.begin + 1]) == 0xBB && static_cast<unsigned char>(text[s.begin + 2]) == 0xBF)
                out.append("\xEF\xBB\xBF");
            out += std::string(StampKey()) + "=" + version;
            out.append(text, s.end, s.next - s.end);   // this line's own ending (or none at the end of the file)
        }
        pos = s.next;
    }
    if (!placed) {
        if (!out.empty() && out.back() != '\n') out += eol;
        out += std::string(StampKey()) + "=" + version + eol;
    }
    return out;
}

/// The lines of `existing` that a writer does not know, to be appended to the text it is about to write so a newer
/// editor's keys (and a person's comments) survive a save by this one. `known(key)` says whether the writer owns a
/// key; lines without '=' (comments) are kept; blank lines and the `engine_version` line are not (the stamp is
/// handled apart). Returned with LF endings.
inline std::string KeepUnknownLines(const std::string& existing, const std::function<bool(const std::string& key)>& known) {
    std::string out, k;
    for (std::size_t pos = 0, n = 0; pos < existing.size(); ++n) {
        const detail::LineSpan s = detail::NextLine(existing, pos);
        pos = s.next;
        std::string line = existing.substr(s.begin, s.end - s.begin);
        if (n == 0 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) line.erase(0, 3);
        if (TrimSpace(line).empty()) continue;
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos) {
            k = std::string(TrimSpace(std::string_view(line).substr(0, eq)));
            if (k == StampKey() || (known && known(k))) continue;
        }
        out += line + "\n";
    }
    return out;
}

/// The whole text of a project.okayproj a writer is about to save: `generated` (what the writer knows how to write),
/// the stamp, then every line of `existing` (the file as it is on disk now) the writer does not own. The stamp goes
/// first: `engine_version=<stamp>`; with an empty `stamp` (a development build, which has no version of its own) the
/// existing line is kept as it is and a file without one gets none. Pass "" for `existing` when there is no file.
inline std::string ComposeProjectFile(const std::string& generated, const std::string& existing, const std::string& stamp,
                                      const std::function<bool(const std::string& key)>& known) {
    std::string out;
    std::string raw;
    if (!stamp.empty()) out = std::string(StampKey()) + "=" + stamp + "\n";
    else if (FindStampInText(existing, &raw) && !raw.empty()) out = std::string(StampKey()) + "=" + raw + "\n";
    out += generated;
    const std::string carried = KeepUnknownLines(existing, known);
    if (!carried.empty()) {
        if (!out.empty() && out.back() != '\n') out += '\n';
        out += carried;
    }
    return out;
}

// ---- the stamp in a project -------------------------------------------------------------------------------------------

/// What <project>/project.okayproj says about the version.
struct Stamp {
    bool fileFound = false;    ///< project.okayproj exists
    bool hasKey = false;       ///< it has an engine_version line
    std::string raw;           ///< the line's value as written ("" when there is none)
    std::string version;       ///< the normalized version ("2.855.0"); "" when there is no line or it is not a version
    std::string error;         ///< the file exists but could not be read
};

inline std::string ProjectFile(const std::string& projectDir) { return okayproj::SettingsPath(projectDir); }

inline Stamp ReadProjectStamp(const std::string& projectDir) {
    Stamp st;
    const std::string path = ProjectFile(projectDir);
    std::error_code ec;
    if (!fs::exists(path, ec) || fs::is_directory(path, ec)) return st;
    std::string text;
    if (!okayproj::ReadText(path, text)) { st.error = "Couldn't read \"" + path + "\"."; return st; }
    st.fileFound = true;
    st.hasKey = FindStampInText(text, &st.raw);
    st.version = NormalizeVersion(st.raw);
    return st;
}

/// The project's stamped version, normalized; "" when there is none (a project made before versions were recorded),
/// when the file is missing or unreadable, or when the line is not a version.
inline std::string ReadProjectVersion(const std::string& projectDir) { return ReadProjectStamp(projectDir).version; }

/// Set the project's `engine_version` and change nothing else in project.okayproj (see SetStampInText). A missing file
/// is created holding just that line. Written whole or not at all (the existing atomic writer); a file that already
/// says `version` is not written at all (so a read-only project that is already stamped is fine); a read-only file
/// that has to change is refused with the reason. `version` must be a version ("v2.855" is stored as "2.855.0").
inline bool WriteProjectVersion(const std::string& projectDir, const std::string& version, std::string* error = nullptr) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    const std::string norm = NormalizeVersion(version);
    if (norm.empty()) return fail("\"" + version + "\" is not a version number.");
    const std::string path = ProjectFile(projectDir);
    std::string old;
    std::error_code ec;
    if (fs::exists(path, ec) && !okayproj::ReadText(path, old)) return fail("Couldn't read \"" + path + "\".");
    const std::string text = SetStampInText(old, norm);
    if (text == old) return true;
    return okayproj::WriteTextAtomic(path, text, error);
}

// ---- where the launcher keeps other versions ---------------------------------------------------------------------------
//
// A release is laid out as   <launcher dir>/OkaySpace.exe (the launcher)  and  <launcher dir>/Tools/OkayEngine.exe (the
// editor, with its own SDL2.dll). The launcher keeps the editors of earlier versions as full copies of Tools:
//   <launcher dir>/Versions/<version>/Tools/OkayEngine.exe        <version> = NormalizeVersion, e.g. "2.850.0"
// An editor started from one of those copies finds the launcher folder three levels up.

/// The launcher's folder, from the folder the running editor's exe is in: <L>/Tools -> <L>; a kept copy,
/// <L>/Versions/<v>/Tools, -> <L>; an editor with no Tools folder around it is taken to sit beside the launcher.
inline fs::path LauncherDirOf(const fs::path& editorExeDir) {
    fs::path p = editorExeDir;
    while (!p.empty() && (p.filename().empty() || p.filename() == ".")) p = p.parent_path();   // a trailing separator
    if (p.filename() == ToolsDirName()) p = p.parent_path();
    if (p.parent_path().filename() == VersionsDirName()) p = p.parent_path().parent_path();
    return p;
}
/// <launcher dir>/Versions
inline fs::path VersionsDir(const fs::path& launcherDir) { return launcherDir / VersionsDirName(); }
/// <launcher dir>/Versions/<version>/Tools/<editor>; empty when `version` is not a version.
inline fs::path InstalledEditorPath(const fs::path& launcherDir, const std::string& version) {
    const std::string v = NormalizeVersion(version);
    if (v.empty()) return {};
    return VersionsDir(launcherDir) / v / ToolsDirName() / EditorExeName();
}
/// True when that editor is there as a file.
inline bool IsInstalled(const fs::path& launcherDir, const std::string& version) {
    const fs::path p = InstalledEditorPath(launcherDir, version);
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
}
/// The versions that have an editor under <launcher dir>/Versions, newest first (a folder whose name is not a version,
/// or that has no editor in it, is not listed).
inline std::vector<std::string> InstalledVersions(const fs::path& launcherDir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(VersionsDir(launcherDir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        const std::string name = it->path().filename().string();
        const std::string v = NormalizeVersion(name);
        if (v.empty() || v != name || !IsInstalled(launcherDir, v)) continue;
        out.push_back(v);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return CompareVersions(a, b) > 0; });
    return out;
}
/// The command line (after the program) that opens `scene` and says the version was chosen on purpose:
/// `--open "<scene>" --keep-version`. The launcher adds `--launcher <pid>` itself.
inline std::vector<std::string> KeepVersionArgs(const std::string& scene) { return {"--open", scene, KeepVersionFlag()}; }

}  // namespace okayver
