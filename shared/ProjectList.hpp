#pragma once
// The launcher's Create-page project list: what a scan shows, and what "Remove from
// list" does to it. Header-only (std only) so the launcher and the tests share it.
// Written from scratch for this engine; no outside code.
//
// The list is: every folder inside the Projects folder that holds <root>/Assets with a
// scene (or still empty), minus the folders the user hid, plus "extra" projects opened
// from other places (stored as scene paths). A project can be in both the folder and the
// extras. Hiding is remembered as a key (KeyOf) so "C:\P\Game", "C:/P/Game/" and, on
// Windows, "c:\p\GAME" all mean the same project.
#include "ProjectDelete.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace okayproj {

constexpr size_t kMaxHiddenProjects = 2000;  // never silently drop: a full list is reported to the user
constexpr size_t kMaxExtraProjects = 400;

#if defined(_WIN32)
constexpr bool kPathsIgnoreCase = true;
#else
constexpr bool kPathsIgnoreCase = false;
#endif

// A comparison key for a folder or file path given as text: both slash kinds become '/',
// runs of slashes collapse, "." parts go, a trailing slash goes, and (Windows) case folds.
// ".." is left alone (it cannot be resolved without the disk).
inline std::string KeyOf(const std::string& path, bool foldCase = kPathsIgnoreCase) {
    std::string out;
    std::string seg;
    std::vector<std::string> parts;
    const bool abs = !path.empty() && (path[0] == '/' || path[0] == '\\');
    auto flush = [&]() { if (!seg.empty() && seg != ".") parts.push_back(seg); seg.clear(); };
    for (char c : path) {
        if (c == '/' || c == '\\') flush();
        else seg.push_back(foldCase ? (char)std::tolower((unsigned char)c) : c);
    }
    flush();
    if (abs) out = "/";
    for (size_t i = 0; i < parts.size(); ++i) { if (i) out += "/"; out += parts[i]; }
    return out;
}

// RootOfScene on text written with either slash kind (a backslash is not a separator on Linux).
inline fs::path SceneRoot(const std::string& scene) { return RootOfScene(fs::path(KeyOf(scene, false))); }
inline std::string RootKey(const std::string& scene, bool foldCase = kPathsIgnoreCase) {
    return KeyOf(SceneRoot(scene).generic_string(), foldCase);
}
inline std::string FolderKey(const fs::path& folder, bool foldCase = kPathsIgnoreCase) {
    return KeyOf(folder.generic_string(), foldCase);
}

// Is the project root `rootKey` hidden? (`hidden` holds KeyOf-style or older raw strings.)
inline bool IsHidden(const std::vector<std::string>& hidden, const std::string& rootKey, bool foldCase = kPathsIgnoreCase) {
    for (const auto& h : hidden) if (KeyOf(h, foldCase) == rootKey) return true;
    return false;
}

// Hide a project root. false = the list is full (nothing was dropped, the caller says so).
inline bool Hide(std::vector<std::string>& hidden, const std::string& rootKey, bool foldCase = kPathsIgnoreCase) {
    if (IsHidden(hidden, rootKey, foldCase)) return true;
    if (hidden.size() >= kMaxHiddenProjects) return false;
    hidden.insert(hidden.begin(), rootKey);
    return true;
}
// Show it again. true = it was hidden.
inline bool Unhide(std::vector<std::string>& hidden, const std::string& rootKey, bool foldCase = kPathsIgnoreCase) {
    const size_t before = hidden.size();
    hidden.erase(std::remove_if(hidden.begin(), hidden.end(),
        [&](const std::string& h) { return KeyOf(h, foldCase) == rootKey; }), hidden.end());
    return hidden.size() != before;
}

// Remember a project opened from elsewhere (by scene path), newest first. false = full.
inline bool AddExtra(std::vector<std::string>& extras, const std::string& scene, bool foldCase = kPathsIgnoreCase) {
    const std::string k = RootKey(scene, foldCase);
    for (const auto& e : extras) if (RootKey(e, foldCase) == k) return true;
    if (extras.size() >= kMaxExtraProjects) return false;
    extras.insert(extras.begin(), scene);
    return true;
}
inline bool RemoveExtra(std::vector<std::string>& extras, const std::string& rootKey, bool foldCase = kPathsIgnoreCase) {
    const size_t before = extras.size();
    extras.erase(std::remove_if(extras.begin(), extras.end(),
        [&](const std::string& e) { return RootKey(e, foldCase) == rootKey; }), extras.end());
    return extras.size() != before;
}

// The scene a project root lists (the launcher's old sceneIn): <name>.okayscene, any scene in
// Assets, or - for an empty Assets - the expected path ("setting up..."). "" = not a project.
inline std::string SceneIn(const fs::path& root) {
    std::error_code e;
    const fs::path assets = root / "Assets";
    if (!fs::is_directory(assets, e)) return "";
    const fs::path preferred = assets / (root.filename().string() + ".okayscene");
    if (fs::exists(preferred, e)) return preferred.string();
    for (fs::directory_iterator it(assets, e), end; !e && it != end; it.increment(e))
        if (it->path().extension() == ".okayscene") return it->path().string();
    return fs::is_empty(assets, e) ? preferred.string() : std::string();
}

// A copy the project upgrade made next to the project ("Name (backup v2.900.0)"). Every finished
// backup holds this marker (kManifestRel in editor/ProjectUpgrade.cpp); a project merely named
// "... (backup ...)" has none, so the name is never used to decide.
inline bool IsUpgradeBackup(const fs::path& root) {
    std::error_code e;
    return fs::is_regular_file(root / ".okay" / "upgrade_backup.txt", e);
}

struct ListedProject { std::string name, scene; bool extra = false; };

// The list. Folder projects first (not hidden), then extras not already listed and not hidden.
inline std::vector<ListedProject> ScanProjects(const fs::path& projectsDir, const std::vector<std::string>& hidden,
                                               const std::vector<std::string>& extras, bool foldCase = kPathsIgnoreCase) {
    std::vector<ListedProject> out;
    std::vector<std::string> seen;
    std::error_code ec;
    for (fs::directory_iterator it(projectsDir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e;
        if (!it->is_directory(e)) continue;
        const std::string key = FolderKey(it->path(), foldCase);
        if (IsHidden(hidden, key, foldCase)) continue;
        if (IsUpgradeBackup(it->path())) continue;   // the Create page lists projects, not upgrade backups
        const std::string sc = SceneIn(it->path());
        if (sc.empty()) continue;
        out.push_back({it->path().filename().string(), sc, false});
        seen.push_back(key);
    }
    for (const auto& ep : extras) {
        const std::string key = RootKey(ep, foldCase);
        if (std::find(seen.begin(), seen.end(), key) != seen.end() || IsHidden(hidden, key, foldCase)) continue;
        if (IsUpgradeBackup(SceneRoot(ep))) continue;
        seen.push_back(key);
        out.push_back({SceneRoot(ep).filename().string(), ep, true});
    }
    return out;
}

enum class ForgetResult { Removed, ListFull };

// "Remove from list" for the project with scene `scene`: it leaves the extras and, when it sits
// in the Projects folder, is hidden so the scan skips it. Files are never touched.
inline ForgetResult ForgetProject(const fs::path& projectsDir, const std::string& scene, std::vector<std::string>& hidden,
                                  std::vector<std::string>& extras, bool foldCase = kPathsIgnoreCase) {
    const std::string key = RootKey(scene, foldCase);
    RemoveExtra(extras, key, foldCase);
    const std::string parent = KeyOf(SceneRoot(scene).parent_path().generic_string(), foldCase);
    if (parent == FolderKey(projectsDir, foldCase) && !Hide(hidden, key, foldCase)) return ForgetResult::ListFull;
    return ForgetResult::Removed;
}

// After the folder was deleted from disk: forget every trace of it.
inline void ForgetDeleted(const std::string& scene, std::vector<std::string>& hidden, std::vector<std::string>& extras,
                          bool foldCase = kPathsIgnoreCase) {
    const std::string key = RootKey(scene, foldCase);
    RemoveExtra(extras, key, foldCase);
    Unhide(hidden, key, foldCase);
}

// launcher.cfg lines. Loading keeps every line up to the caps; saving writes every entry.
inline bool ApplyPrefLine(const std::string& k, const std::string& v, std::vector<std::string>& extras, std::vector<std::string>& hidden) {
    if (k == "extra_project") { if (!v.empty() && extras.size() < kMaxExtraProjects) extras.push_back(v); return true; }
    if (k == "hidden_project") { if (!v.empty() && hidden.size() < kMaxHiddenProjects) hidden.push_back(v); return true; }
    return false;
}
inline void WritePrefLines(std::ostream& f, const std::vector<std::string>& extras, const std::vector<std::string>& hidden) {
    for (const auto& ep : extras) f << "extra_project=" << ep << "\n";
    for (const auto& hp : hidden) f << "hidden_project=" << hp << "\n";
}

}  // namespace okayproj
