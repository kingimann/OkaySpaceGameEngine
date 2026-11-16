#pragma once
// Clipboard and drag and drop, as data. Pure logic, no OS.
//  * ClipboardProvider: what a backend's clipboard offers, by MIME type ("text/plain", "text/uri-list", ...),
//    with an in-memory implementation (MemoryClipboardProvider) the tests and a headless run use. The OS
//    backends (X11 selections, Win32 CF_*) are meant to implement the same interface.
//  * ParseUriList / FileUriToPath: the "text/uri-list" text X11 drops and Linux file managers put on the
//    clipboard ("file:///home/a%20b.txt"), and the same for a Windows "file:///C:/x" form.
//  * DropAssembler: turns "a drop of N files" into DropBegin, DropFile * N, DropComplete events, the way SDL does.
#include "okay/Platform/Platform.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace okay::platform {

/// True when `s` is well-formed UTF-8 (no overlong forms, no surrogates, nothing past U+10FFFF).
inline bool IsValidUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = (unsigned char)s[i];
        int n; uint32_t cp;
        if (c < 0x80) { i++; continue; }
        else if (c >= 0xC2 && c <= 0xDF) { n = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { n = 2; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { n = 3; cp = c & 0x07; }
        else return false;
        for (int k = 1; k <= n; k++) {
            if (i + k >= s.size()) return false;
            const unsigned char d = (unsigned char)s[i + k];
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if ((n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += n + 1;
    }
    return true;
}

inline constexpr const char* kMimeText = "text/plain";
inline constexpr const char* kMimeUriList = "text/uri-list";

class ClipboardProvider {
public:
    virtual ~ClipboardProvider() = default;
    virtual bool Has(const std::string& mime) const = 0;
    /// False when `mime` is not offered.
    virtual bool Get(const std::string& mime, std::vector<uint8_t>& out) const = 0;
    /// Replace the whole clipboard by one format. Empty `mime` or data over the limit is refused.
    virtual bool Set(const std::string& mime, const std::vector<uint8_t>& data) = 0;
    virtual void Clear() = 0;
    virtual std::vector<std::string> Formats() const = 0;
    /// Counts up on every change, so an app can poll "did the clipboard change".
    virtual uint64_t ChangeCount() const = 0;

    // Text helpers, UTF-8. A text that is not valid UTF-8 is refused (a Windows backend converts first).
    bool SetText(const std::string& text) {
        if (!IsValidUtf8(text)) return false;
        return Set(kMimeText, std::vector<uint8_t>(text.begin(), text.end()));
    }
    std::string GetText() const {
        std::vector<uint8_t> d;
        if (!Get(kMimeText, d)) return std::string();
        return std::string(d.begin(), d.end());
    }
};

class MemoryClipboardProvider final : public ClipboardProvider {
public:
    static constexpr size_t kMaxBytes = 16u * 1024 * 1024;
    bool Has(const std::string& mime) const override { return m_data.count(mime) != 0; }
    bool Get(const std::string& mime, std::vector<uint8_t>& out) const override {
        auto it = m_data.find(mime);
        if (it == m_data.end()) return false;
        out = it->second;
        return true;
    }
    bool Set(const std::string& mime, const std::vector<uint8_t>& data) override {
        if (mime.empty() || data.size() > kMaxBytes) return false;
        m_data.clear();
        m_data[mime] = data;
        m_changes++;
        return true;
    }
    void Clear() override { if (!m_data.empty()) { m_data.clear(); m_changes++; } }
    std::vector<std::string> Formats() const override {
        std::vector<std::string> v;
        for (auto& kv : m_data) v.push_back(kv.first);
        return v;
    }
    uint64_t ChangeCount() const override { return m_changes; }
private:
    std::map<std::string, std::vector<uint8_t>> m_data;
    uint64_t m_changes = 0;
};

/// "file:///a%20b" -> "/a b"; "file:///C:/x/y" -> "C:/x/y"; "file://localhost/a" -> "/a". Empty string when the
/// text is not a local file URI (other scheme, remote host, bad %XX, an encoded NUL).
inline std::string FileUriToPath(const std::string& uri) {
    const std::string pre = "file://";
    if (uri.compare(0, pre.size(), pre) != 0) return std::string();
    size_t i = pre.size();
    const size_t slash = uri.find('/', i);
    const std::string host = uri.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
    if (!host.empty() && host != "localhost") return std::string();
    if (slash == std::string::npos) return std::string();
    std::string out;
    for (i = slash; i < uri.size(); i++) {
        const char c = uri[i];
        if (c == '%') {
            if (i + 2 >= uri.size()) return std::string();
            auto hx = [](char h) { return h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1; };
            const int a = hx(uri[i + 1]), b = hx(uri[i + 2]);
            if (a < 0 || b < 0) return std::string();
            const int v = a * 16 + b;
            if (v == 0) return std::string();
            out += (char)v;
            i += 2;
        } else if (c == '?' || c == '#') {
            break;   // query and fragment are not part of a path
        } else {
            out += c;
        }
    }
    // "/C:/x" is a Windows drive path.
    if (out.size() >= 3 && out[0] == '/' && ((out[1] >= 'A' && out[1] <= 'Z') || (out[1] >= 'a' && out[1] <= 'z')) && out[2] == ':')
        out.erase(0, 1);
    return out;
}

/// The local paths in a text/uri-list body: one URI per line, '#' lines are comments, CR LF or LF endings.
/// URIs that are not local files are skipped. At most `maxFiles` paths come back.
inline std::vector<std::string> ParseUriList(const std::string& body, size_t maxFiles = 4096) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < body.size() && out.size() < maxFiles) {
        size_t e = body.find('\n', pos);
        if (e == std::string::npos) e = body.size();
        std::string line = body.substr(pos, e - pos);
        pos = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::string p = FileUriToPath(line);
        if (!p.empty()) out.push_back(p);
    }
    return out;
}

/// Builds the event sequence of one drop: DropBegin, one DropFile per path (text = path), DropComplete.
/// An empty list gives no events.
inline std::vector<Event> MakeDropEvents(const std::vector<std::string>& paths, uint32_t windowId, uint32_t timestampMs = 0) {
    std::vector<Event> ev;
    if (paths.empty()) return ev;
    Event e;
    e.windowId = windowId; e.timestampMs = timestampMs;
    e.type = EventType::DropBegin; ev.push_back(e);
    for (const std::string& p : paths) { e.type = EventType::DropFile; e.text = p; ev.push_back(e); }
    e.text.clear(); e.type = EventType::DropComplete; ev.push_back(e);
    return ev;
}

} // namespace okay::platform
