#pragma once
// What window mode (OKAY_UI_HOST=okayui, docs/ui/own_ui_toolkit.md 6.24 step 3) remembers between runs, without Dear ImGui's ini: where the window was and how
// big, which panels are open, and the dock layout (OkayUI's own DockLayout text). One small text file; own format, written from what has to be kept.
// Everything read from it is untrusted: a damaged, huge or hand-edited file can never give a window off screen or a panel name that is not a name.
//
//     okayhost 1
//     window <x> <y> <w> <h>
//     open <Name with %XX escapes> <0|1>
//     layout
//     <the DockLayout text, to the end of the file>
//
// No <mutex>/<thread> (Windows build rule); file access is <fstream> / <filesystem> only.
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>

namespace okayuihost {

struct WindowState {
    int x = INT_MIN, y = INT_MIN;     // INT_MIN: let the window manager place it
    int w = 1280, h = 800;
    bool operator==(const WindowState& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

struct PersistedState {
    WindowState window;
    std::map<std::string, bool> open;   // panel name -> open (a panel that is not listed uses its default)
    std::string layout;                 // DockLayout::Save() text, empty = none saved
    bool operator==(const PersistedState& o) const { return window == o.window && open == o.open && layout == o.layout; }
};

constexpr std::size_t kMaxStateBytes = 256 * 1024;   // a bigger file is not ours
constexpr std::size_t kMaxPanels = 64;
constexpr std::size_t kMaxPanelName = 96;

inline std::string EscapeName(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) { if (c <= 0x20 || c == '%' || c == 0x7f) { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; } else o += (char)c; }
    return o;
}
inline std::string UnescapeName(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2;
        } else o += s[i];
    }
    return o;
}

inline std::string Serialize(const PersistedState& st) {
    std::ostringstream o;
    o << "okayhost 1\n";
    o << "window " << (st.window.x == INT_MIN ? 0 : st.window.x) << " " << (st.window.y == INT_MIN ? 0 : st.window.y) << " " << st.window.w << " " << st.window.h
      << " " << (st.window.x == INT_MIN ? 0 : 1) << "\n";   // last field: 1 when the position is known
    size_t n = 0;
    for (const auto& kv : st.open) { if (++n > kMaxPanels) break; o << "open " << EscapeName(kv.first) << " " << (kv.second ? 1 : 0) << "\n"; }
    if (!st.layout.empty()) o << "layout\n" << st.layout << (st.layout.back() == '\n' ? "" : "\n");
    return o.str();
}

/// Read a state. False (and `out` untouched) when the text is not a state at all (no header, too big); otherwise every field that reads cleanly is
/// taken and the rest keeps its default.
inline bool Parse(const std::string& text, PersistedState& out) {
    if (text.size() > kMaxStateBytes || text.compare(0, 10, "okayhost 1") != 0) return false;
    PersistedState st;
    std::istringstream in(text);
    std::string line;
    std::getline(in, line);   // the header
    bool inLayout = false;
    while (std::getline(in, line)) {
        if (inLayout) { st.layout += line; st.layout += '\n'; continue; }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "layout") { inLayout = true; continue; }
        std::istringstream ls(line);
        std::string key; ls >> key;
        if (key == "window") {
            long long x = 0, y = 0, w = 0, h = 0; int known = 0;
            if (ls >> x >> y >> w >> h) {
                ls >> known;
                if (w >= 1 && w <= 16384 && h >= 1 && h <= 16384) { st.window.w = (int)w; st.window.h = (int)h; }
                if (known == 1 && x > -100000 && x < 100000 && y > -100000 && y < 100000) { st.window.x = (int)x; st.window.y = (int)y; }
            }
        } else if (key == "open") {
            std::string name; int v = -1;
            if (ls >> name >> v && (v == 0 || v == 1) && st.open.size() < kMaxPanels) {
                name = UnescapeName(name);
                if (!name.empty() && name.size() <= kMaxPanelName) st.open[name] = v == 1;
            }
        }
    }
    out = st;
    return true;
}

/// Fit a remembered window onto the display: at least 320 x 200, not bigger than the display, and with a good part on it (a window left on a monitor that is
/// gone comes back to the display's top left). `display` is the usable area (x, y, w, h); a zero area means "unknown": only the size limits apply.
inline WindowState Sanitize(WindowState w, int dx, int dy, int dw, int dh) {
    w.w = std::max(320, std::min(w.w, 16384)); w.h = std::max(200, std::min(w.h, 16384));
    if (dw <= 0 || dh <= 0) return w;
    w.w = std::min(w.w, dw); w.h = std::min(w.h, dh);
    if (w.x == INT_MIN || w.y == INT_MIN) { w.x = INT_MIN; w.y = INT_MIN; return w; }
    const long long minVisible = 120;   // pixels of the window that must be on the display, each way
    const bool offX = (long long)w.x + w.w < dx + minVisible || (long long)w.x > (long long)dx + dw - minVisible;
    const bool offY = (long long)w.y < dy - 10 || (long long)w.y > (long long)dy + dh - minVisible;
    if (offX || offY) { w.x = dx + std::max(0, (dw - w.w) / 2); w.y = dy + std::max(0, (dh - w.h) / 2); }
    return w;
}

inline bool LoadFile(const std::filesystem::path& path, PersistedState& out) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || std::filesystem::file_size(path, ec) > kMaxStateBytes) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return Parse(text, out);
}

/// Write through a temporary file and rename it over the old one, so a crash leaves either the old state or the new one. False on any failure (the old
/// file is then untouched).
inline bool SaveFileAtomic(const std::filesystem::path& path, const PersistedState& st) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        const std::string text = Serialize(st);
        if (text.size() > kMaxStateBytes) { f.close(); std::filesystem::remove(tmp, ec); return false; }
        f << text;
        f.flush();
        if (!f) { f.close(); std::filesystem::remove(tmp, ec); return false; }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { std::filesystem::remove(path, ec); ec.clear(); std::filesystem::rename(tmp, path, ec); }   // (rename over an existing file is refused on some systems)
    if (ec) { std::filesystem::remove(tmp, ec); return false; }
    return true;
}

/// Saves a state at most every `intervalMs` while it keeps changing, and once more when asked to flush (the window is closing). The clock is passed in, so a
/// test controls it.
class StateSaver {
public:
    StateSaver(std::filesystem::path path, unsigned intervalMs = 2000) : m_path(std::move(path)), m_interval(intervalMs) {}
    /// Offer the current state; returns true when it was written now.
    bool Offer(const PersistedState& st, unsigned long long nowMs) {
        if (m_haveSaved && st == m_saved) return false;
        if (m_haveSaved && nowMs < m_lastMs + m_interval) return false;
        return Write(st, nowMs);
    }
    bool Flush(const PersistedState& st, unsigned long long nowMs) { if (m_haveSaved && st == m_saved) return false; return Write(st, nowMs); }
    int Writes() const { return m_writes; }
    const std::filesystem::path& Path() const { return m_path; }
private:
    bool Write(const PersistedState& st, unsigned long long nowMs) {
        m_lastMs = nowMs;
        if (!SaveFileAtomic(m_path, st)) return false;
        m_saved = st; m_haveSaved = true; ++m_writes;
        return true;
    }
    std::filesystem::path m_path;
    unsigned m_interval;
    PersistedState m_saved; bool m_haveSaved = false;
    unsigned long long m_lastMs = 0;
    int m_writes = 0;
};

} // namespace okayuihost
