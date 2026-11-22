#pragma once
// Layered file lookup: find a game file in the downloaded content first, then in the installed game files. Docs:
// docs/release/content_delivery.md "Overlay". A plain function on top of the existing path rules (okay/Core/PathSafety.hpp); it does not
// replace Paths, SaveData or any loader: code that wants the overlay calls Overlay::Resolve for the name it was about to open.
//
//   * Only names from the VERIFIED, committed manifest are served (the tree folder holds nothing else, and the lookup goes through the
//     manifest's own list), so a stray file dropped into the content folder is never found.
//   * The content lookup is case-insensitive on every platform (the manifest refuses two names that differ only by case), like Windows
//     file names, so a game made on Windows finds "Textures/Ship.PNG" for "textures/ship.png" on any machine.
//   * Names go through okay::pathsafety first (no "..", no absolute path, no drive letter, no device name); a name that fails is not
//     found in either layer. Symbolic links that point out of a folder are refused (StaysInside).
//   * Load() verifies the manifest signature again, so content that is not signed by a trusted key is never served.
// Set up once at start-up (Load), before loaders run; after that it is read-only, so there is no lock and no <mutex>.
#include <filesystem>
#include <string>
#include <unordered_map>

#include "ContentSync.hpp"

namespace okaycontent {

class Overlay {
public:
    /// Read the committed version under `root` and index it. False (with `why`) leaves the overlay EMPTY: every lookup then falls back to
    /// the installed files, which is the safe state.
    bool Load(const std::string& root, const std::string& game, const std::string& trustedKeys, bool allowUnsigned = false, std::string* why = nullptr,
              const Limits& lim = Limits()) {
        Clear();
        auto fail = [&](const char* m) { if (why) *why = m; return false; };
        StorePaths sp{root};
        Commit c;
        if (!ReadCommit(sp, c)) return fail("no downloaded content");
        if (c.game != game) return fail("the downloaded content is for another game");
        Manifest m;
        std::string text;
        if (!ReadCommittedManifest(sp, c, m, &text, lim)) return fail("the saved content list is missing or damaged");
        std::string sig;
        detail::ReadAll(sp.SigFile(c.version), sig, okayupdate::kMaxSigBytes);
        const SigVerdict v = CheckSignature(trustedKeys, text, sig, allowUnsigned);
        if (!v.Accepts()) { if (why) *why = v.message; return false; }
        std::error_code ec;
        const std::filesystem::path tree = sp.Tree(c.version);
        if (!std::filesystem::is_directory(tree, ec)) return fail("the content files of the saved version are missing");
        for (const Entry& e : m.entries) m_files[detail::Lower(e.path)] = e.path;
        m_tree = tree.string();
        m_version = c.version;
        return true;
    }
    void Clear() { m_files.clear(); m_tree.clear(); m_version = 0; }
    bool Active() const { return !m_tree.empty(); }
    std::uint64_t Version() const { return m_version; }
    std::size_t FileCount() const { return m_files.size(); }

    /// The downloaded file for `rel`, or "" (not downloaded, unsafe name, not a regular file).
    std::string Find(const std::string& rel) const {
        if (!Active()) return std::string();
        std::string name;
        if (!Normalize(rel, name)) return std::string();
        const auto it = m_files.find(detail::Lower(name));
        if (it == m_files.end()) return std::string();
        std::error_code ec;
        const std::filesystem::path p = detail::P(m_tree) / detail::P(it->second);
        if (!std::filesystem::is_regular_file(p, ec)) return std::string();
        if (!okay::pathsafety::StaysInside(detail::P(m_tree), it->second)) return std::string();
        return p.generic_string();
    }

    /// Downloaded first, then `installRoot`/`rel`. "" when neither has it (or the name is unsafe). `fromContent` says which layer answered.
    std::string Resolve(const std::string& rel, const std::string& installRoot, bool* fromContent = nullptr) const {
        if (fromContent) *fromContent = false;
        std::string name;
        if (!Normalize(rel, name)) return std::string();
        const std::string d = Find(name);
        if (!d.empty()) { if (fromContent) *fromContent = true; return d; }
        std::error_code ec;
        const std::filesystem::path p = detail::P(installRoot) / detail::P(name);
        if (!std::filesystem::is_regular_file(p, ec)) return std::string();
        if (!okay::pathsafety::StaysInside(detail::P(installRoot), name)) return std::string();
        return p.generic_string();
    }

    /// The one shared overlay of the running game (set by the player at start-up).
    static Overlay& Global() { static Overlay o; return o; }

private:
    static bool Normalize(const std::string& in, std::string& out) {
        out = in;
        for (char& c : out) if (c == '\\') c = '/';
        return PathProblem(out).empty();
    }
    std::unordered_map<std::string, std::string> m_files;   // lower-cased path -> path as the manifest spells it
    std::string m_tree;
    std::uint64_t m_version = 0;
};

}  // namespace okaycontent
