#pragma once
// The Project panel's tab strip as plain data (no ImGui, no files of its own except one small text file):
//   Assets      the folder view
//   Favourites  the starred files and folders of the project (flat list)
//   Recent      what changed in the last week (the existing "Recently Changed" smart view)
//   Search      results of the search box (shown as the active tab while the box has text)
// Which tab is on screen follows from three facts the panel already has (the smart view id, the search text),
// so there is one source of truth. The last tab and folder are remembered per project in
// <project>/.okay/projectview.txt. Written from the panel's own behaviour; no outside code.
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace oked::projecttabs {

enum class Tab { Assets = 0, Favourites = 1, Recent = 2, Search = 3 };

// Smart view ids the panel already uses: -1 = folder view, 100 = Recently Changed, 101 = Unused Assets,
// 1..8 = type views, 200+ = colour labels. 102 is the new Favourites view.
inline constexpr int kSmartNone = -1;
inline constexpr int kSmartRecent = 100;
inline constexpr int kSmartFavourites = 102;

inline const char* TabName(Tab t) {
    switch (t) {
        case Tab::Assets: return "Assets";
        case Tab::Favourites: return "Favourites";
        case Tab::Recent: return "Recent";
        case Tab::Search: return "Search";
    }
    return "Assets";
}

/// Which tab is active. Text in the search box wins (a search runs over what the other tabs would show).
inline Tab ActiveTab(int smart, const std::string& searchText) {
    const bool hasText = searchText.find_first_not_of(" \t") != std::string::npos;
    if (hasText) return Tab::Search;
    if (smart == kSmartFavourites) return Tab::Favourites;
    if (smart == kSmartRecent) return Tab::Recent;
    return Tab::Assets;
}

/// The smart view id a click on a tab asks for (Search keeps the current one: it is entered by typing).
inline int SmartForTab(Tab t, int currentSmart) {
    switch (t) {
        case Tab::Assets: return kSmartNone;
        case Tab::Favourites: return kSmartFavourites;
        case Tab::Recent: return kSmartRecent;
        case Tab::Search: return currentSmart;
    }
    return kSmartNone;
}

/// Whether the folder-only commands (new folder, paste, rename in place) make sense on this tab.
inline bool TabIsFolderView(Tab t) { return t == Tab::Assets || t == Tab::Search; }

/// Number badge text: "" for zero, "99+" above 99.
inline std::string CountBadge(int n) {
    if (n <= 0) return "";
    if (n > 99) return "99+";
    return std::to_string(n);
}

/// What is remembered per project.
struct Remembered {
    int smart = kSmartNone;      ///< only the tab-like views are kept (none, Recent, Favourites)
    std::string folder;          ///< relative to Assets, '/' separated, "" = the Assets folder
};

inline std::string Serialize(const Remembered& r) {
    std::ostringstream o;
    const int s = (r.smart == kSmartRecent || r.smart == kSmartFavourites) ? r.smart : kSmartNone;
    o << "smart " << s << "\nfolder " << r.folder << "\n";
    return o.str();
}

inline Remembered Parse(const std::string& text) {
    Remembered r;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("smart ", 0) == 0) {
            const int v = std::atoi(line.c_str() + 6);
            r.smart = (v == kSmartRecent || v == kSmartFavourites) ? v : kSmartNone;
        } else if (line.rfind("folder ", 0) == 0) {
            std::string f = line.substr(7);
            // never accept an escape from Assets or an absolute path from a hand-edited file
            std::replace(f.begin(), f.end(), '\\', '/');
            const bool bad = (!f.empty() && (f[0] == '/' || (f.size() > 1 && f[1] == ':'))) ||
                             f == ".." || f.rfind("../", 0) == 0 || f.find("/../") != std::string::npos ||
                             (f.size() >= 3 && f.compare(f.size() - 3, 3, "/..") == 0);
            r.folder = bad ? std::string() : f;
        }
    }
    return r;
}

inline bool Save(const std::string& file, const Remembered& r) {
    std::ofstream o(file, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o << Serialize(r);
    return static_cast<bool>(o);
}

inline Remembered Load(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::ostringstream s; s << in.rdbuf();
    return Parse(s.str());
}

}  // namespace oked::projecttabs
