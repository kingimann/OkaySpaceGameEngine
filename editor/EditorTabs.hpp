#pragma once
// Panel tabs and layouts: the pure (ImGui-free) half of the editor's tab system,
// so it can be unit tested. The drawing side lives in editor/main.cpp.
//
//  - Saved layouts: the user's named layouts (ImGui dock ini text + the set of
//    open panels), written to okay_layouts.txt.
//  - Built-in layout presets (Default, Tall, Wide, 2 by 3, Scripting, Animation).
//  - Most-recently-used panel order and wrap-around cycling for Ctrl+Tab.
//  - Filter matching for the tab overflow list and the Ctrl+Tab switcher.
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace okay::edtabs {

// ---- Built-in layout presets -------------------------------------------------
enum class Preset { Default, Tall, Wide, TwoByThree, Scripting, Animation, Count };
inline constexpr int kPresetCount = (int)Preset::Count;

inline const char* PresetName(int p) {
    static const char* k[kPresetCount] = {"Default", "Tall", "Wide", "2 by 3", "Scripting", "Animation"};
    return (p >= 0 && p < kPresetCount) ? k[p] : "";
}
inline const char* PresetHint(int p) {
    static const char* k[kPresetCount] = {
        "Hierarchy left, Inspector right, Console and Project along the bottom.",
        "Scene over Game on the left; Hierarchy, Project and Inspector in tall columns.",
        "A wide Scene; Hierarchy, Project and Console side by side underneath.",
        "Scene over Game, then Hierarchy, Project and Inspector as three columns.",
        "A big Script Editor in the middle with Project, Console and Inspector around it.",
        "Scene on top, a wide Animation timeline and Animator underneath.",
    };
    return (p >= 0 && p < kPresetCount) ? k[p] : "";
}

inline std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// -1 when `name` isn't a built-in preset (case-insensitive).
inline int PresetFromName(const std::string& name) {
    const std::string n = Lower(name);
    for (int i = 0; i < kPresetCount; ++i)
        if (n == Lower(PresetName(i))) return i;
    return -1;
}

// ---- Saved (user) layouts ----------------------------------------------------
struct SavedLayout {
    std::string name;
    std::string ini;                 // ImGui::SaveIniSettingsToMemory() text
    std::vector<std::string> open;   // labels of the panels that were open
};

// A name fit for the layout menu: control characters dropped, whitespace
// collapsed and trimmed, at most 40 characters. Empty means "not a usable name".
inline std::string CleanLayoutName(const std::string& in) {
    std::string out;
    bool space = false;
    for (char ch : in) {
        unsigned char c = (unsigned char)ch;
        if (c < 32 || c == 127 || c == ' ') { space = !out.empty(); continue; }
        if (space) { out += ' '; space = false; }
        out += ch;
    }
    if (out.size() > 40) {
        out.resize(40);
        // Don't leave half a UTF-8 sequence at the end: find the last character's
        // lead byte and drop that character if the cut took some of its bytes.
        std::size_t lead = out.size();
        while (lead > 0 && ((unsigned char)out[lead - 1] & 0xC0) == 0x80) --lead;
        if (lead > 0) {
            const unsigned char b = (unsigned char)out[lead - 1];
            const std::size_t want = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
            if (out.size() - (lead - 1) < want) out.resize(lead - 1);
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
    }
    return out;
}

// Index of the layout called `name` (case-insensitive), or -1.
inline int FindLayout(const std::vector<SavedLayout>& v, const std::string& name) {
    const std::string n = Lower(name);
    for (std::size_t i = 0; i < v.size(); ++i)
        if (Lower(v[i].name) == n) return (int)i;
    return -1;
}

// Add a layout, replacing one with the same name. Returns true when it replaced.
inline bool UpsertLayout(std::vector<SavedLayout>& v, SavedLayout l) {
    const int i = FindLayout(v, l.name);
    if (i >= 0) { v[(std::size_t)i] = std::move(l); return true; }
    v.push_back(std::move(l));
    return false;
}

inline bool RemoveLayout(std::vector<SavedLayout>& v, const std::string& name) {
    const int i = FindLayout(v, name);
    if (i < 0) return false;
    v.erase(v.begin() + i);
    return true;
}

// File format (text, versioned):
//   okaylayouts 1
//   layout <name>
//   open <label>|<label>|...
//   ini <byte count>
//   <exactly that many bytes of ini text>
//   end
// The ini block is length-prefixed so its [sections] and newlines need no escaping.
inline std::string SerializeLayouts(const std::vector<SavedLayout>& v) {
    std::string out = "okaylayouts 1\n";
    for (const SavedLayout& l : v) {
        out += "layout " + CleanLayoutName(l.name) + "\n";
        out += "open ";
        bool first = true;
        for (const std::string& o : l.open) {
            if (o.empty() || o.find('|') != std::string::npos || o.find('\n') != std::string::npos) continue;
            if (!first) out += '|';
            out += o; first = false;
        }
        out += "\n";
        out += "ini " + std::to_string(l.ini.size()) + "\n";
        out += l.ini;
        out += "\nend\n";
    }
    return out;
}

// Reads what SerializeLayouts wrote. Unknown versions and damaged entries are
// skipped (a truncated file keeps every layout before the damage).
inline std::vector<SavedLayout> ParseLayouts(const std::string& text) {
    std::vector<SavedLayout> out;
    std::size_t pos = 0;
    auto readLine = [&](std::string& line) -> bool {
        if (pos >= text.size()) return false;
        std::size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        line = text.substr(pos, e - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = e + 1;
        return true;
    };
    std::string line;
    if (!readLine(line) || line.rfind("okaylayouts ", 0) != 0) return out;
    if (std::atoi(line.c_str() + 12) != 1) return out;
    SavedLayout cur;
    bool inLayout = false;
    while (readLine(line)) {
        if (line.rfind("layout ", 0) == 0) {
            cur = SavedLayout{};
            cur.name = CleanLayoutName(line.substr(7));
            inLayout = !cur.name.empty();
        } else if (inLayout && line.rfind("open", 0) == 0) {
            std::string rest = line.size() > 5 ? line.substr(5) : std::string();
            std::stringstream ss(rest);
            std::string item;
            while (std::getline(ss, item, '|'))
                if (!item.empty()) cur.open.push_back(item);
        } else if (inLayout && line.rfind("ini ", 0) == 0) {
            const long long n = std::atoll(line.c_str() + 4);
            if (n < 0 || pos + (std::size_t)n > text.size()) return out;   // truncated
            cur.ini = text.substr(pos, (std::size_t)n);
            pos += (std::size_t)n;
            if (pos < text.size() && text[pos] == '\n') ++pos;
        } else if (inLayout && line == "end") {
            UpsertLayout(out, cur);
            inLayout = false;
        }
    }
    return out;
}

// ---- Panel order: most recently used + cycling -------------------------------
// Move `name` to the front of the most-recently-used list (capped).
inline void TouchMru(std::vector<std::string>& mru, const std::string& name, std::size_t cap = 64) {
    if (name.empty()) return;
    if (!mru.empty() && mru.front() == name) return;
    mru.erase(std::remove(mru.begin(), mru.end(), name), mru.end());
    mru.insert(mru.begin(), name);
    if (mru.size() > cap) mru.resize(cap);
}

// The open panels, most recently used first; panels never focused keep their
// given order after those.
inline std::vector<std::string> MruOrder(const std::vector<std::string>& mru,
                                         const std::vector<std::string>& open) {
    std::vector<std::string> out;
    out.reserve(open.size());
    for (const std::string& m : mru)
        if (std::find(open.begin(), open.end(), m) != open.end() &&
            std::find(out.begin(), out.end(), m) == out.end())
            out.push_back(m);
    for (const std::string& o : open)
        if (std::find(out.begin(), out.end(), o) == out.end()) out.push_back(o);
    return out;
}

// Step `cur` by `step` through `count` items, wrapping both ways. -1 when empty.
inline int CycleIndex(int cur, int count, int step) {
    if (count <= 0) return -1;
    int i = (cur + step) % count;
    return i < 0 ? i + count : i;
}

// Case-insensitive: every space-separated word of `query` appears in `label`.
// An empty query matches everything.
inline bool MatchesFilter(const std::string& label, const std::string& query) {
    const std::string l = Lower(label);
    std::stringstream ss(Lower(query));
    std::string word;
    while (ss >> word)
        if (l.find(word) == std::string::npos) return false;
    return true;
}

// Whether a tab bar's tabs at their full width don't fit (they get squeezed or
// scroll), so the overflow list button is worth showing. Both widths leave the
// button itself out. A little slack keeps it from flickering while a splitter is
// dragged across the boundary.
inline bool TabsOverflow(float idealWidth, float barWidth, int tabCount, bool shownLastFrame, float slack = 6.0f) {
    if (tabCount < 2 || barWidth <= 0.0f) return false;
    return shownLastFrame ? idealWidth > barWidth - slack : idealWidth > barWidth;
}

}  // namespace okay::edtabs
