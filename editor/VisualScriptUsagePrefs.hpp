#pragma once
// What the Visual Script window remembers about the person using it: the start card is hidden for good, and
// which first-step hints they have already done. Plain text (one "key value" per line), so a test can round-trip
// it. Saved next to okay_settings.txt as okay_vs_usage.txt (a small file of its own, so editor/main.cpp is not
// touched). No ImGui, no threads.
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace okay::editor::vsed {

struct VsUsagePrefs {
    bool hideStart = false;      ///< "Do not show this again" on the start card
    unsigned hintsDone = 0;      ///< bit per first-step hint (VisualScriptHints.hpp)
    bool operator==(const VsUsagePrefs& o) const { return hideStart == o.hideStart && hintsDone == o.hintsDone; }
};

inline std::string VsUsagePrefsToText(const VsUsagePrefs& p) {
    std::ostringstream o;
    o << "hidestart " << (p.hideStart ? 1 : 0) << "\n" << "hintsdone " << p.hintsDone << "\n";
    return o.str();
}

/// Reads the text; anything unknown or unreadable is ignored (the defaults stay).
inline VsUsagePrefs VsUsagePrefsFromText(const std::string& text) {
    VsUsagePrefs p;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string key = line.substr(0, sp), val = line.substr(sp + 1);
        char* end = nullptr;
        const unsigned long v = std::strtoul(val.c_str(), &end, 10);
        if (end == val.c_str()) continue;
        if (key == "hidestart") p.hideStart = v != 0;
        else if (key == "hintsdone") p.hintsDone = (unsigned)(v & 0xFFFFu);
    }
    return p;
}

inline VsUsagePrefs VsUsagePrefsLoad(const std::string& path = "okay_vs_usage.txt") {
    std::ifstream f(path);
    if (!f.good()) return {};
    std::stringstream ss; ss << f.rdbuf();
    return VsUsagePrefsFromText(ss.str());
}
inline bool VsUsagePrefsSave(const VsUsagePrefs& p, const std::string& path = "okay_vs_usage.txt") {
    std::ofstream f(path, std::ios::trunc);
    if (!f.good()) return false;
    f << VsUsagePrefsToText(p);
    return f.good();
}

} // namespace okay::editor::vsed
