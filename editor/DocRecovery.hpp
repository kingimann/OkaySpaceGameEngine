#pragma once
// Editor documents (graphs, scripts, the open scene) share the engine's
// RecoveryManager and BackupManager. This header only packs several buffers
// into the one payload those classes already know how to rotate, checksum
// and restore. It does not write files itself.
#include "okay/Core/Backup.hpp"
#include "okay/Core/Recovery.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace okay::editor::docrec {

struct Buffer {
    std::string kind;   ///< "graph", "script", "scene", "closed" (a closed graph) or "closed-script"
    std::string key;    ///< path, or "untitled:<id>" when the document was never saved
    std::string text;
};

inline std::string EncodeBundle(const std::vector<Buffer>& docs) {
    std::string o = "OKAYREC1\n";
    o += std::to_string(docs.size());
    o += '\n';
    for (const Buffer& d : docs) {
        o += d.kind; o += '\n';
        o += d.key; o += '\n';
        o += std::to_string(d.text.size()); o += '\n';
        o += d.text;
        o += '\n';
    }
    return o;
}

inline bool DecodeBundle(const std::string& in, std::vector<Buffer>& out, std::string* error = nullptr) {
    out.clear();
    std::size_t i = 0;
    auto line = [&](std::string& s) -> bool {
        if (i >= in.size()) return false;
        std::size_t e = in.find('\n', i);
        if (e == std::string::npos) return false;
        s = in.substr(i, e - i);
        if (!s.empty() && s.back() == '\r') s.pop_back();
        i = e + 1;
        return true;
    };
    std::string magic, countText;
    if (!line(magic) || magic != "OKAYREC1" || !line(countText)) {
        if (error) *error = "not a recovery bundle";
        return false;
    }
    char* end = nullptr;
    long count = std::strtol(countText.c_str(), &end, 10);
    if (end == countText.c_str() || count < 0 || count > 100000) {
        if (error) *error = "bad document count";
        return false;
    }
    for (long n = 0; n < count; ++n) {
        Buffer b;
        std::string sizeText;
        if (!line(b.kind) || !line(b.key) || !line(sizeText)) {
            if (error) *error = "truncated bundle";
            return false;
        }
        char* se = nullptr;
        long sz = std::strtol(sizeText.c_str(), &se, 10);
        if (se == sizeText.c_str() || sz < 0 || i + (std::size_t)sz > in.size()) {
            if (error) *error = "bad payload size";
            return false;
        }
        b.text.assign(in, i, (std::size_t)sz);
        i += (std::size_t)sz;
        if (i < in.size() && in[i] == '\n') ++i;
        out.push_back(std::move(b));
    }
    return true;
}

/// A recovered buffer is worth offering when it is not identical to the copy
/// already on disk (or when there is no file yet).
inline bool BufferWorthRestoring(const std::string& recovered, const std::string& onDisk, bool fileExists) {
    if (!fileExists) return !recovered.empty();
    return recovered != onDisk;
}

/// Older editors wrote `<scene>.autosave` beside the scene file. New sessions
/// do not. RecoveryManager is the recovery copy. This stays false so a test
/// can catch a regression that starts writing the sidecar again.
inline bool WritesLegacySceneSidecar() { return false; }

/// Path of a leftover `<scene>.autosave` that is newer than the scene, or empty.
/// Reads only. A missing scene counts as older than any sidecar, matching the
/// prompt that used to run on open.
inline std::string NewerLegacySceneAutosave(const std::string& scenePath) {
    if (scenePath.empty()) return {};
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path side(scenePath + ".autosave");
    // A missing sidecar is not an error. exists() clears ec in that case;
    // is_regular_file() can leave ENOENT set, which would hide a real file.
    if (!fs::exists(side, ec) || ec || !fs::is_regular_file(side, ec) || ec) return {};
    const auto sideTime = fs::last_write_time(side, ec);
    if (ec) return {};
    auto sceneTime = fs::file_time_type::min();
    ec.clear();
    if (fs::exists(scenePath, ec) && !ec) {
        sceneTime = fs::last_write_time(scenePath, ec);
        if (ec) return {};
    }
    if (sideTime > sceneTime) return side.string();
    return {};
}

/// Backups of one file: the newest `keepLast`, and no extra daily copies.
inline BackupPolicy LastSavesPolicy(int keepLast = 5) {
    BackupPolicy p;
    p.keepLast = keepLast < 1 ? 1 : keepLast;
    p.keepDailyDays = 0;
    p.maxTotalBytes = 0;
    return p;
}

/// One closed document, so Ctrl+Shift+T can bring it back this session.
/// The same text is also packed into the recovery bundle (kind "closed") until
/// the user quits cleanly, so a kill after Don't Save can still restore it.
struct ClosedDoc {
    std::string kind;
    std::string key;
    std::string path;
    std::string text;
    std::string baseline;   ///< the saved text it differed from
};

inline std::vector<ClosedDoc>& ClosedStack() {
    static std::vector<ClosedDoc> s;
    return s;
}

inline void RememberClosed(ClosedDoc doc) {
    auto& s = ClosedStack();
    s.insert(s.begin(), std::move(doc));
    if (s.size() > 15) s.resize(15);
}

inline bool TakeClosed(ClosedDoc& out) {
    auto& s = ClosedStack();
    if (s.empty()) return false;
    out = std::move(s.front());
    s.erase(s.begin());
    return true;
}

// ---- The "Restore recovered work?" question ---------------------------------------------------------------------
// (owner, test build 2.903.8.128: after an unclean exit "only Discard works"; Restore and Compare did nothing.)

/// Where a recovered buffer goes back to.
enum class Role { Graph, Script, Scene, Unknown };

/// "closed" meant a closed graph, but closed scripts were packed under the same kind, so Restore opened a script's text
/// as a visual-script graph. New bundles say "closed-script"; an older bundle's "closed" entry is a graph only when it
/// looks like one (an untitled graph key, a .okayvs path, or the graph text header).
inline bool LooksLikeGraph(const Buffer& b) {
    if (b.key.rfind("untitled:", 0) == 0) return true;
    const std::string ext = ".okayvs";
    std::string low = b.key;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    if (low.size() >= ext.size() && low.compare(low.size() - ext.size(), ext.size(), ext) == 0) return true;
    return b.text.rfind("# OkayVS graph", 0) == 0;
}
inline Role RoleOf(const Buffer& b) {
    if (b.kind == "graph") return Role::Graph;
    if (b.kind == "script" || b.kind == "closed-script") return Role::Script;
    if (b.kind == "scene") return Role::Scene;
    if (b.kind == "closed") return LooksLikeGraph(b) ? Role::Graph : Role::Script;
    return Role::Unknown;
}

/// One crashed session's recovery copy, read and unpacked once (the question used to read and decode the file again
/// on every frame it was open). A session leaves up to `slots` files; they are one offer, not one question per file
/// (Restore on the newest and then Restore on the next, older one put older text over newer).
struct Offer {
    std::string sessionId;
    std::string label;                                 ///< what the editor stored ("2 unsaved")
    std::int64_t timestamp = 0;                        ///< when the copy that Restore uses was written
    std::vector<RecoveryManager::Candidate> files;     ///< every file of the session, newest first; Discard removes them all
    std::vector<Buffer> docs;                          ///< what Restore brings back
    bool readable = false;                             ///< false: only Discard (or keep it for later) is offered
    std::string error;                                 ///< why it can't be read, in words for the question
    std::string note;                                  ///< e.g. the newest file was damaged and an older one is used
};

/// Group the candidates (FindRecoverable's list, newest first) by session and unpack each session's newest file that
/// reads. A file whose bundle can't be unpacked falls back to the session's next file; when none reads, the offer is
/// kept with `readable` false and the reason, so the question can say so instead of a Restore button that does nothing.
inline std::vector<Offer> MakeOffers(const RecoveryManager& rm, const std::vector<RecoveryManager::Candidate>& found) {
    std::vector<Offer> out;
    for (const auto& c : found) {
        Offer* o = nullptr;
        for (auto& e : out) if (e.sessionId == c.sessionId) { o = &e; break; }
        if (!o) { out.emplace_back(); o = &out.back(); o->sessionId = c.sessionId; o->label = c.label; o->timestamp = c.timestamp; }
        o->files.push_back(c);
    }
    for (auto& o : out) {
        std::string firstErr;
        for (std::size_t i = 0; i < o.files.size(); ++i) {
            std::string payload, err;
            std::vector<Buffer> docs;
            if (rm.Restore(o.files[i], payload, &err) && DecodeBundle(payload, docs, &err)) {
                o.docs = std::move(docs);
                o.readable = true;
                o.label = o.files[i].label;
                o.timestamp = o.files[i].timestamp;
                if (i > 0) o.note = "The newest copy could not be read (" + firstErr + "); this is the one saved before it.";
                break;
            }
            if (firstErr.empty()) firstErr = err.empty() ? std::string("unknown reason") : err;
        }
        if (!o.readable) o.error = firstErr.empty() ? std::string("the recovery file is gone") : firstErr;
    }
    return out;
}

/// Delete every file of the offer. Returns how many could not be deleted.
inline int DiscardOffer(RecoveryManager& rm, const Offer& o) {
    int left = 0;
    for (const auto& f : o.files) {
        std::error_code ec;
        if (!rm.Discard(f) && std::filesystem::exists(f.path, ec)) ++left;
    }
    return left;
}

/// Where Restore puts each buffer back (the editor passes its tabs and its scene). Each returns false with `error`.
struct RestoreSinks {
    std::function<bool(const Buffer&, std::string& error)> graph, script, scene;
};
struct RestoreOutcome {
    int restored = 0;
    std::vector<std::string> failed;   ///< "<kind> <key>: <why>" for each buffer that did not come back
    bool Ok() const { return failed.empty(); }
    std::string Describe() const {
        std::string s;
        for (const auto& f : failed) { if (!s.empty()) s += '\n'; s += f; }
        return s;
    }
};

/// Put every buffer back. The scene goes first: loading a scene does not touch the script or graph tabs, so the
/// documents opened after it stay open. A failure is reported, never skipped silently (the copy must then be kept).
inline RestoreOutcome ApplyBundle(const std::vector<Buffer>& docs, const RestoreSinks& to) {
    RestoreOutcome r;
    auto put = [&](const Buffer& b) {
        const Role role = RoleOf(b);
        const auto& fn = role == Role::Graph ? to.graph : role == Role::Script ? to.script : to.scene;
        std::string err;
        if (role == Role::Unknown) err = "unknown kind of document";
        else if (!fn) err = "this editor can't open it";
        else if (fn(b, err)) { ++r.restored; return; }
        if (err.empty()) err = "it could not be opened";
        r.failed.push_back(b.kind + " " + b.key + ": " + err);
    };
    for (const auto& b : docs) if (RoleOf(b) == Role::Scene) put(b);
    for (const auto& b : docs) if (RoleOf(b) != Role::Scene) put(b);
    return r;
}

/// The Compare rows: one per recovered buffer that differs from its file on disk (or has no file yet). Read once when
/// Compare is pressed, not every frame.
inline std::vector<std::string> CompareRows(const std::vector<Buffer>& docs) {
    std::vector<std::string> rows;
    for (const auto& b : docs) {
        std::string disk;
        bool exists = false;
        if (!b.key.empty() && b.key.rfind("untitled", 0) != 0) {
            std::ifstream f(b.key, std::ios::binary);
            exists = (bool)f;
            if (f) { std::stringstream ss; ss << f.rdbuf(); disk = ss.str(); }
        }
        if (!BufferWorthRestoring(b.text, disk, exists)) continue;
        rows.push_back(b.kind + "  " + b.key + "\nRecovered (" + std::to_string(b.text.size()) + " characters). On disk: " +
                       (exists ? "different" : "no file yet") + ".");
    }
    return rows;
}

}  // namespace okay::editor::docrec
