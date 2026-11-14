#include "okay/Core/DisplayModes.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace okay {
namespace {

std::string Trim(std::string s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    std::size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool SameName(const std::string& a, const std::string& b) {
    return Lower(Trim(a)) == Lower(Trim(b));
}

bool InRange(int w, int h) {
    return w >= kDisplayMinWidth && h >= kDisplayMinHeight && w <= kDisplayMaxWidth && h <= kDisplayMaxHeight;
}

bool ByMode(const DisplayMode& a, const DisplayMode& b) {
    const long long aa = (long long)a.width * a.height, bb = (long long)b.width * b.height;
    if (aa != bb) return aa < bb;
    if (a.width != b.width) return a.width < b.width;
    if (a.height != b.height) return a.height < b.height;
    return a.refreshHz < b.refreshHz;
}

int ClampRefresh(int hz) {
    if (hz < 0) return 0;
    if (hz > 1000) return 1000;
    return hz;
}

bool ReadInt(const std::string& text, int& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (!end || end == text.c_str() || *end != '\0') return false;
    out = (int)v;
    return true;
}

} // namespace

void DisplayTrial::Start(float seconds) {
    if (active) return;
    if (!(seconds > 0.f)) seconds = 10.f;
    active = true;
    secondsLeft = seconds;
}

void DisplayTrial::Clear() {
    active = false;
    secondsLeft = 0.f;
}

bool DisplayTrial::Tick(float dt) {
    if (!active) return false;
    if (!(dt > 0.f)) return false;
    secondsLeft -= dt;
    if (secondsLeft > 0.f) return false;
    secondsLeft = 0.f;
    active = false;
    return true;
}

std::vector<DisplayMode> FilterDisplayModes(std::vector<DisplayMode> modes) {
    modes.erase(std::remove_if(modes.begin(), modes.end(), [](const DisplayMode& m) {
        return !InRange(m.width, m.height) || m.refreshHz < 0 || m.refreshHz > 1000;
    }), modes.end());
    std::sort(modes.begin(), modes.end(), ByMode);
    modes.erase(std::unique(modes.begin(), modes.end()), modes.end());
    return modes;
}

std::vector<DisplayResolution> ResolutionsFromModes(const std::vector<DisplayMode>& modes) {
    std::vector<DisplayResolution> out;
    for (const DisplayMode& m : FilterDisplayModes(std::vector<DisplayMode>(modes))) {
        const DisplayResolution size = m.Size();
        if (out.empty() || out.back() != size) out.push_back(size);
    }
    return out;
}

std::vector<int> RefreshRatesFor(const std::vector<DisplayMode>& modes, int width, int height) {
    std::vector<int> out;
    for (const DisplayMode& m : modes) {
        if (m.width != width || m.height != height || m.refreshHz <= 0 || m.refreshHz > 1000) continue;
        if (std::find(out.begin(), out.end(), m.refreshHz) == out.end()) out.push_back(m.refreshHz);
    }
    std::sort(out.begin(), out.end());
    return out;
}

int NearestRefresh(const std::vector<int>& rates, int want) {
    if (rates.empty() || want < 1) return 0;
    int best = rates[0];
    int bestDist = std::abs(rates[0] - want);
    for (int r : rates) {
        const int d = std::abs(r - want);
        if (d < bestDist || (d == bestDist && r < best)) { best = r; bestDist = d; }
    }
    return best;
}

DisplayMode NearestDisplayMode(const std::vector<DisplayMode>& modes, int width, int height, int refreshHz) {
    const std::vector<DisplayMode> list = FilterDisplayModes(std::vector<DisplayMode>(modes));
    refreshHz = ClampRefresh(refreshHz);
    if (list.empty()) {
        if (!InRange(width, height)) return {};
        return {width, height, refreshHz};
    }
    DisplayMode best = list[0];
    double bestDist = -1.0;
    for (const DisplayMode& m : list) {
        if (m.width == width && m.height == height) { best = m; bestDist = 0.0; break; }
        const double dw = (double)m.width - width, dh = (double)m.height - height;
        const double d = dw * dw + dh * dh;
        if (bestDist < 0.0 || d < bestDist || (d == bestDist && ByMode(m, best))) { best = m; bestDist = d; }
    }
    const int w = best.width, h = best.height;
    if (refreshHz < 1) return {w, h, 0};
    const std::vector<int> rates = RefreshRatesFor(list, w, h);
    if (rates.empty()) return {w, h, 0};
    for (int r : rates) if (r == refreshHz) return {w, h, r};
    return {w, h, NearestRefresh(rates, refreshHz)};
}

int ChooseMonitorIndex(const std::vector<DisplayMonitor>& monitors, const std::string& name, int savedIndex, bool& replaced) {
    replaced = false;
    if (monitors.empty()) return -1;
    const std::string want = Trim(name);
    if (!want.empty()) {
        for (std::size_t i = 0; i < monitors.size(); ++i)
            if (SameName(monitors[i].name, want)) return (int)i;
        replaced = true;
        return 0;
    }
    if (savedIndex >= 0 && savedIndex < (int)monitors.size()) return savedIndex;
    if (savedIndex >= (int)monitors.size()) { replaced = true; return 0; }
    return -1;
}

DisplaySaved ConfirmedOrPrevious(const DisplaySaved& saved) {
    if (!saved.pending || !saved.hasPrevious) {
        DisplaySaved s = saved;
        s.pending = false;
        return s;
    }
    DisplaySaved s;
    s.mode = saved.prevMode;
    s.width = saved.prevWidth;
    s.height = saved.prevHeight;
    s.refreshHz = saved.prevRefresh;
    s.monitorName = saved.prevMonitorName;
    s.monitorIndex = saved.prevMonitorIndex;
    return s;
}

DisplayPick ResolveDisplay(const DisplaySaved& savedIn, const std::vector<DisplayMonitor>& monitors) {
    const DisplaySaved saved = ConfirmedOrPrevious(savedIn);
    DisplayPick pick;
    std::string modeText = Lower(Trim(saved.mode));
    modeText.erase(std::remove_if(modeText.begin(), modeText.end(), [](char c) { return std::isspace((unsigned char)c) != 0; }), modeText.end());
    if (modeText == "borderless" || modeText == "fullscreen_desktop" || modeText == "fullscreen-desktop" || modeText == "1")
        pick.mode = WindowMode::Borderless;
    else if (modeText == "fullscreen" || modeText == "exclusive" || modeText == "2")
        pick.mode = WindowMode::Fullscreen;
    else
        pick.mode = WindowMode::Windowed;

    bool replaced = false;
    const int idx = ChooseMonitorIndex(monitors, saved.monitorName, saved.monitorIndex, replaced);
    pick.monitorReplaced = replaced;
    if (idx >= 0) {
        pick.monitorIndex = idx;
        pick.monitorName = monitors[(std::size_t)idx].name;
    } else {
        pick.monitorIndex = saved.monitorIndex;
        pick.monitorName = saved.monitorName;
    }

    const std::vector<DisplayMode> modes = idx >= 0 ? monitors[(std::size_t)idx].modes : std::vector<DisplayMode>{};
    if (saved.width <= 0 || saved.height <= 0) return pick;
    // A window or a borderless window may be any allowed size. Exclusive full screen has to be
    // a mode that monitor actually lists.
    if (pick.mode != WindowMode::Fullscreen) {
        if (!InRange(saved.width, saved.height)) {
            pick.width = std::clamp(saved.width, kDisplayMinWidth, kDisplayMaxWidth);
            pick.height = std::clamp(saved.height, kDisplayMinHeight, kDisplayMaxHeight);
            pick.modeReplaced = true;
        } else {
            pick.width = saved.width;
            pick.height = saved.height;
        }
        pick.refreshHz = ClampRefresh(saved.refreshHz);
        return pick;
    }
    const DisplayMode chosen = NearestDisplayMode(modes, saved.width, saved.height, saved.refreshHz);
    if (!chosen.Valid()) return pick;
    if (chosen.width != saved.width || chosen.height != saved.height) pick.modeReplaced = true;
    if (saved.refreshHz > 0 && chosen.refreshHz != saved.refreshHz) pick.modeReplaced = true;
    pick.width = chosen.width;
    pick.height = chosen.height;
    pick.refreshHz = chosen.refreshHz;
    return pick;
}

std::string SerializeDisplay(const DisplaySaved& saved) {
    std::ostringstream out;
    auto line = [&](const std::string& key, const std::string& value) {
        if (!value.empty()) out << key << '=' << value << '\n';
    };
    auto num = [&](const std::string& key, int value, bool when) {
        if (when) out << key << '=' << value << '\n';
    };
    line("mode", saved.mode);
    num("width", saved.width, saved.width > 0);
    num("height", saved.height, saved.height > 0);
    num("refresh", saved.refreshHz, saved.refreshHz > 0);
    line("monitor", saved.monitorName);
    num("monitor_index", saved.monitorIndex, saved.monitorIndex >= 0);
    if (saved.pending) out << "pending=1\n";
    if (saved.hasPrevious) {
        line("prev_mode", saved.prevMode);
        num("prev_width", saved.prevWidth, saved.prevWidth > 0);
        num("prev_height", saved.prevHeight, saved.prevHeight > 0);
        num("prev_refresh", saved.prevRefresh, saved.prevRefresh > 0);
        line("prev_monitor", saved.prevMonitorName);
        num("prev_monitor_index", saved.prevMonitorIndex, saved.prevMonitorIndex >= 0);
    }
    return out.str();
}

bool ParseDisplay(const std::string& text, DisplaySaved& out) {
    out = DisplaySaved{};
    bool any = false;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t end = text.find('\n', i);
        if (end == std::string::npos) end = text.size();
        std::string line = Trim(text.substr(i, end - i));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        i = end + 1;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        int n = 0;
        if (key == "mode") { out.mode = value; any = true; }
        else if (key == "width" && ReadInt(value, n)) { out.width = n; any = true; }
        else if (key == "height" && ReadInt(value, n)) { out.height = n; any = true; }
        else if (key == "refresh" && ReadInt(value, n)) { out.refreshHz = n; any = true; }
        else if (key == "monitor") { out.monitorName = value; any = true; }
        else if (key == "monitor_index" && ReadInt(value, n)) { out.monitorIndex = n; any = true; }
        else if (key == "pending") { out.pending = value == "1" || Lower(value) == "true"; any = true; }
        else if (key == "prev_mode") { out.prevMode = value; out.hasPrevious = true; any = true; }
        else if (key == "prev_width" && ReadInt(value, n)) { out.prevWidth = n; out.hasPrevious = true; any = true; }
        else if (key == "prev_height" && ReadInt(value, n)) { out.prevHeight = n; out.hasPrevious = true; any = true; }
        else if (key == "prev_refresh" && ReadInt(value, n)) { out.prevRefresh = n; out.hasPrevious = true; any = true; }
        else if (key == "prev_monitor") { out.prevMonitorName = value; out.hasPrevious = true; any = true; }
        else if (key == "prev_monitor_index" && ReadInt(value, n)) { out.prevMonitorIndex = n; out.hasPrevious = true; any = true; }
    }
    return any;
}

} // namespace okay
