#pragma once
// Pure logic for the Win32 clipboard, drag and drop and IME composition of the own Windows backend: byte layouts
// and text conversions, so they run in unit tests on any host (tests/test_platform_win32_pure.cpp). No windows.h.
// Written from the Win32 documentation of the DROPFILES structure (CF_HDROP) and of the IMM composition string
// (GCS_COMPSTR in UTF-16 code units, GCS_CURSORPOS as a count of those units).
#include "Exchange.hpp"
#include "PlatformText.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace okay::platform::win32 {

// DROPFILES: DWORD pFiles (offset of the list), POINT pt (two LONG), BOOL fNC, BOOL fWide: 20 bytes.
constexpr size_t kDropFilesHeader = 20;
constexpr size_t kMaxDropPaths = 4096;
constexpr size_t kMaxDropBytes = 64u * 1024 * 1024;

/// A CF_HDROP blob for `paths` (UTF-8, forward or back slashes): the DROPFILES header with fWide set, then every
/// path as UTF-16 and NUL-terminated, then one more NUL. Empty paths are skipped. `ptX`, `ptY`: drop point.
inline std::vector<uint8_t> BuildDropFiles(const std::vector<std::string>& paths, int ptX = 0, int ptY = 0) {
    std::vector<uint8_t> b(kDropFilesHeader, 0);
    auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + (size_t)i] = (uint8_t)(v >> (8 * i)); };
    put32(0, (uint32_t)kDropFilesHeader);
    put32(4, (uint32_t)ptX); put32(8, (uint32_t)ptY);
    put32(16, 1);   // fWide
    size_t n = 0;
    for (const std::string& p : paths) {
        if (p.empty() || n >= kMaxDropPaths) continue;
        std::u16string w = text::Utf8ToUtf16(p);
        for (char16_t& c : w) if (c == u'/') c = u'\\';
        for (char16_t c : w) { b.push_back((uint8_t)(c & 0xFF)); b.push_back((uint8_t)(c >> 8)); }
        b.push_back(0); b.push_back(0);
        n++;
    }
    b.push_back(0); b.push_back(0);   // the list ends with an empty string
    return b;
}

/// The paths in a CF_HDROP blob, as UTF-8. Bounds-checked: a bad offset, a missing terminator or a hostile size
/// gives what was readable before the problem. Handles both wide and ANSI lists (an ANSI list is read as Latin-1,
/// the closest we can do without the system code page).
inline std::vector<std::string> ParseDropFiles(const uint8_t* data, size_t size, int* ptX = nullptr, int* ptY = nullptr) {
    std::vector<std::string> out;
    if (!data || size < kDropFilesHeader || size > kMaxDropBytes) return out;
    auto get32 = [&](size_t at) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)data[at + (size_t)i] << (8 * i); return v; };
    const uint32_t off = get32(0);
    if (ptX) *ptX = (int)get32(4);
    if (ptY) *ptY = (int)get32(8);
    const bool wide = get32(16) != 0;
    if (off < kDropFilesHeader || off > size) return out;
    size_t pos = off;
    if (wide) {
        while (pos + 2 <= size && out.size() < kMaxDropPaths) {
            std::u16string w;
            bool terminated = false;
            while (pos + 2 <= size) {
                const char16_t c = (char16_t)(data[pos] | (data[pos + 1] << 8));
                pos += 2;
                if (c == 0) { terminated = true; break; }
                w += c;
            }
            if (!terminated || w.empty()) break;   // an empty string ends the list; a cut-off last one is dropped
            out.push_back(text::Utf16ToUtf8(w.data(), w.size()));
        }
    } else {
        while (pos < size && out.size() < kMaxDropPaths) {
            std::string s;
            bool terminated = false;
            while (pos < size) {
                const uint8_t c = data[pos++];
                if (c == 0) { terminated = true; break; }
                text::AppendUtf8(s, c);
            }
            if (!terminated || s.empty()) break;
            out.push_back(s);
        }
    }
    return out;
}

/// "C:\a b\c.txt" -> "file:///C:/a%20b/c.txt"; "/home/a" -> "file:///home/a". Everything outside the unreserved set
/// and '/' and ':' is percent-encoded (UTF-8 bytes).
inline std::string PathToFileUri(const std::string& pathIn) {
    std::string path = pathIn;
    for (char& c : path) if (c == '\\') c = '/';
    static const char* hex = "0123456789ABCDEF";
    std::string out = "file://";
    if (path.empty() || path[0] != '/') out += '/';   // a drive path gets the empty host and a leading slash
    for (unsigned char c : path) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':';
        if (plain) out += (char)c; else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

/// CF_HDROP blob -> "text/uri-list" body (CR LF after every URI).
inline std::string DropFilesToUriList(const uint8_t* data, size_t size) {
    std::string out;
    for (const std::string& p : ParseDropFiles(data, size)) { out += PathToFileUri(p); out += "\r\n"; }
    return out;
}
/// "text/uri-list" body -> CF_HDROP blob (only local files; empty when there are none).
inline std::vector<uint8_t> UriListToDropFiles(const std::string& uriList) {
    const std::vector<std::string> paths = ParseUriList(uriList);
    if (paths.empty()) return {};
    return BuildDropFiles(paths);
}

// ---- IME composition ---------------------------------------------------------------------------
/// GCS_CURSORPOS counts UTF-16 units; the Event wants characters (code points). Clamped to the text.
inline int ImeCursorToChars(const std::u16string& comp, int cursorUnits) {
    if (cursorUnits <= 0) return 0;
    int chars = 0;
    size_t i = 0;
    while (i < comp.size() && (int)i < cursorUnits) {
        if (comp[i] >= 0xD800 && comp[i] < 0xDC00 && i + 1 < comp.size() && comp[i + 1] >= 0xDC00 && comp[i + 1] < 0xE000) i += 2; else i++;
        chars++;
    }
    return chars;
}

/// The TextEditing event for a composition string (UTF-16 as the IMM gives it). An empty string ends the composition.
inline Event MakeCompositionEvent(const std::u16string& comp, int cursorUnits, uint32_t windowId, uint32_t timestampMs = 0) {
    Event e;
    e.type = EventType::TextEditing; e.windowId = windowId; e.timestampMs = timestampMs;
    e.text = text::Utf16ToUtf8(comp.data(), comp.size());
    e.editStart = ImeCursorToChars(comp, cursorUnits);
    e.editLength = 0;
    return e;
}

} // namespace okay::platform::win32
