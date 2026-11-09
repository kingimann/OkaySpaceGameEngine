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
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace okay::editor::docrec {

struct Buffer {
    std::string kind;   ///< "graph", "script", "scene" or "closed"
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

}  // namespace okay::editor::docrec
