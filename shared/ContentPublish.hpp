#pragma once
// Live Updates, the publisher side: turn a folder of content into a manifest, and publish a new version to any storage provider
// (ContentTransport.hpp: a local or LAN folder, OkayBackend Storage, ...) sending only the files that changed. Design note:
// docs/design/live_updates.md. Used by `okay-content make-manifest` / `okay-content publish` and, later, the editor's Live Updates window.
//
// Order of a publish: check everything first (the manifest, the signature against the game's own key when given, every local file's
// size and SHA-256, the version number goes up), then upload the new and changed files (and any unchanged file the server lost), then
// the signature, then the manifest LAST: a client sees the old version or the new one, never a manifest naming files that are not there
// yet. Files the new version no longer lists stay on the server (a client still finishing the previous version may need them); they are
// reported in `delta.removed`.
//
// Written for this project; the file-kind table moved here from tools/okay-content/main.cpp. Signing is NOT done here: the caller passes
// the signature text made by the existing MakeSignatureFile (ContentManifest.hpp). Header only, no threads.
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ContentSync.hpp"

namespace okaycontent {

/// The manifest kind of a file, by extension. `known` false for a type that is not content (refused by ScanFolder).
inline Kind KindForPath(const std::string& path, bool& known) {
    known = true;
    std::string ext;
    const std::size_t dot = path.rfind('.');
    if (dot != std::string::npos) ext = detail::Lower(path.substr(dot + 1));
    static const std::map<std::string, Kind> m = {
        {"glb", Kind::Model}, {"gltf", Kind::Model}, {"obj", Kind::Model}, {"fbx", Kind::Model}, {"dae", Kind::Model}, {"bin", Kind::Model}, {"mtl", Kind::Model}, {"okaymodel", Kind::Model},
        {"png", Kind::Texture}, {"jpg", Kind::Texture}, {"jpeg", Kind::Texture}, {"tga", Kind::Texture}, {"bmp", Kind::Texture}, {"okaymat", Kind::Texture}, {"ktx", Kind::Texture},
        {"okayanim", Kind::Animation}, {"bvh", Kind::Animation}, {"okayclips", Kind::Animation}, {"okaycutscene", Kind::Animation},
        {"okayscene", Kind::Scene}, {"okayprefab", Kind::Scene},
        {"wav", Kind::Audio}, {"ogg", Kind::Audio}, {"mp3", Kind::Audio}, {"flac", Kind::Audio},
        {"okaydialog", Kind::Dialogue}, {"okayquest", Kind::Dialogue}, {"okayloc", Kind::Dialogue},
        {"okayvs", Kind::Script}, {"okayscript", Kind::Script}, {"okaybt", Kind::Script},
        {"okaydata", Kind::Data}, {"okayitems", Kind::Data}, {"okayweapon", Kind::Data}, {"json", Kind::Data}, {"txt", Kind::Data}, {"csv", Kind::Data},
        {"okaylive", Kind::Config}};
    const auto it = m.find(ext);
    if (it == m.end()) { known = false; return Kind::Data; }
    return it->second;
}

/// Every content file under `dir` as manifest entries (path order), skipping content.manifest(.sig) and names starting with '.'.
/// `groups` maps a path prefix to a group name. Refused files (links, unknown types, bad names) go to `problems`; the caller decides.
inline std::vector<Entry> ScanFolder(const std::string& dir, const std::vector<std::pair<std::string, std::string>>& groups, std::vector<std::string>& problems,
                                     const Limits& lim = Limits()) {
    std::vector<Entry> out;
    const fs::path root = detail::P(dir);
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string rel = fs::relative(it->path(), root, ec).generic_u8string();
        if (rel.empty() || rel[0] == '.') { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }   // our own bookkeeping
        if (it->is_symlink(ec)) { problems.push_back(rel + " is a link"); continue; }
        if (!it->is_regular_file(ec)) continue;
        if (rel == kManifestName || rel == kSigName) continue;
        Entry e;
        e.path = rel;
        bool known = false;
        e.kind = KindForPath(rel, known);
        if (!known) { problems.push_back(rel + " (unknown file type; content is models, textures, animations, scenes, audio, dialogue, scripts, data and live config only)"); continue; }
        e.size = static_cast<std::uint64_t>(it->file_size(ec));
        e.sha256 = detail::FileSha256Hex(it->path());
        for (const auto& g : groups) if (rel.compare(0, g.first.size(), g.first) == 0) e.group = g.second;
        if (std::string w = EntryProblem(e, lim); !w.empty()) { problems.push_back(w); continue; }
        out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const Entry& x, const Entry& y) { return x.path < y.path; });
    return out;
}

struct PublishOptions {
    std::string verifyKeys;       ///< the game's public key(s): the signature must verify under them before anything is sent ("" = skip)
    bool allowUnsigned = false;   ///< publish with no signature (a local test; a shipped game refuses it)
    Limits limits;
};

struct PublishResult {
    bool ok = false;
    std::string message;          ///< plain words
    std::uint64_t previous = 0;   ///< the version the server had (0 = none)
    Delta delta;                  ///< from the server's version to the new one
    int uploaded = 0;             ///< content files sent (manifest and signature not counted)
    int repaired = 0;             ///< unchanged files the server did not have, sent again
    std::uint64_t bytesUploaded = 0;
};

/// Publish `next` (its files are in `folder`, laid out by path) to `store`. `sigText` is content.manifest.sig for Serialize(next).
inline PublishResult PublishVersion(Transport& store, const std::string& folder, const Manifest& next, const std::string& sigText, const PublishOptions& opt = PublishOptions()) {
    PublishResult r;
    auto fail = [&](const std::string& m) { r.ok = false; r.message = m; return r; };
    if (std::string w = Problem(next, opt.limits); !w.empty()) return fail("The new content list is not valid: " + w);
    const std::string text = Serialize(next);
    if (sigText.empty() && !opt.allowUnsigned) return fail("The new content list is not signed. Nothing was published.");
    if (!opt.verifyKeys.empty()) {
        const SigVerdict v = CheckSignature(opt.verifyKeys, text, sigText);
        if (!v.Accepts()) return fail("The signature does not match the game's key: " + v.message + " Nothing was published.");
    }
    // what the server has now
    Manifest live;
    {
        std::string liveText;
        const WholeGet w = GetWhole(store, kManifestName, opt.limits.maxManifestBytes, liveText);
        if (w.Ok()) {
            std::string why;
            if (!Parse(liveText, live, &why, opt.limits)) return fail("The content list on the server is damaged (" + why + "). Nothing was published.");
            if (live.game != next.game) return fail("The server holds content of the game '" + live.game + "', not '" + next.game + "'. Nothing was published.");
            if (next.version <= live.version)
                return fail("The new version (" + std::to_string(next.version) + ") must be higher than the server's (" + std::to_string(live.version) + "). Nothing was published.");
            r.previous = live.version;
        } else if (w.status != 404) {   // 404 = nothing published yet
            return fail("Could not read the server's content list (" + (w.result.error.empty() ? "status " + std::to_string(w.status) : w.result.error) + "). Nothing was published.");
        }
    }
    r.delta = Diff(live, next);
    // every file we send must be exactly what the new list says
    std::vector<Entry> send = r.delta.added;
    send.insert(send.end(), r.delta.changed.begin(), r.delta.changed.end());
    std::vector<Listed> have;
    if (store.List("", have).ok) {   // a provider that can list: send again any unchanged file it lost
        std::map<std::string, std::uint64_t> sizes;
        for (const Listed& l : have) sizes[l.name] = l.size;
        std::map<std::string, bool> sending;
        for (const Entry& e : send) sending[e.path] = true;
        for (const Entry& e : next.entries) {
            if (sending.count(e.path)) continue;
            const auto it = sizes.find(e.path);
            if (it == sizes.end() || (it->second != 0 && it->second != e.size)) { send.push_back(e); ++r.repaired; }
        }
    }
    for (const Entry& e : send)
        if (!detail::FileHasSizeAndHash(detail::P(folder) / detail::P(e.path), e, /*hash=*/true))
            return fail("The local file '" + e.path + "' does not match the new content list (size or SHA-256). Nothing was published.");
    for (const Entry& e : send) {
        const StoreResult p = store.Put(e.path, (detail::P(folder) / detail::P(e.path)).u8string());
        if (!p.ok) return fail("Could not upload '" + e.path + "': " + p.error + ". The server still serves version " + std::to_string(r.previous) + ".");
        ++r.uploaded;
        r.bytesUploaded += e.size;
    }
    r.uploaded -= r.repaired;
    // signature, then the manifest: the switch
    const fs::path scratch = detail::P(folder) / ".okay-publish";
    std::error_code ec;
    fs::create_directories(scratch, ec);
    auto putText = [&](const char* name, const std::string& bytes) {
        const fs::path f = scratch / name;
        if (!detail::WriteAtomic(f, bytes)) { StoreResult s; s.error = "cannot write a temporary file"; return s; }
        return store.Put(name, f.u8string());
    };
    if (!sigText.empty()) {
        const StoreResult s = putText(kSigName, sigText);
        if (!s.ok) { fs::remove_all(scratch, ec); return fail(std::string("Could not upload the signature: ") + s.error + "."); }
    }
    const StoreResult m = putText(kManifestName, text);
    fs::remove_all(scratch, ec);
    if (!m.ok) return fail(std::string("Could not upload the content list: ") + m.error + ". Clients keep the version they have.");
    r.ok = true;
    r.message = "Published content version " + std::to_string(next.version) + " (" + std::to_string(r.delta.added.size()) + " new, " + std::to_string(r.delta.changed.size()) +
                " changed, " + std::to_string(r.delta.removed.size()) + " removed, " + std::to_string(r.delta.unchanged) + " unchanged; " + std::to_string(r.bytesUploaded) +
                " bytes sent).";
    return r;
}

}  // namespace okaycontent
