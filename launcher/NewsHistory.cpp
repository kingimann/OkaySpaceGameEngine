// What's New history (NewsHistory.hpp): every release's notes this launcher has seen, kept in news.cfg. Moved out of
// launcher/main.cpp unchanged (docs/engine/modularization_plan.md, Part C section 8, batch L4).
#include "LauncherShared.hpp"
#include "NewsHistory.hpp"
#include "TestBuild.hpp"
#include <algorithm>

namespace okl {

// Launcher preferences persisted next to the exe (launcher.cfg).
// ---- What's New history --------------------------------------------------
// Every release's notes this launcher has seen (newest first, up to 12), kept
// in news.cfg so the What's New page can show earlier releases too. Lines:
// "v\t<version>" starts a release, "l\t<text>" is one of its notes.
std::vector<NewsRelease> g_newsHistory;
std::string g_newsSeen;            // newest version whose notes were opened (prefs)
static void SaveNewsHistory() {
    std::ofstream f(fs::path(g_exeDir) / "news.cfg", std::ios::trunc);
    for (const auto& r : g_newsHistory) {
        f << "v\t" << r.ver << "\n";
        for (const auto& l : r.lines) f << "l\t" << l << "\n";
    }
}
void LoadNewsHistory() {
    g_newsHistory.clear();
    std::ifstream f(fs::path(g_exeDir) / "news.cfg");
    std::string ln;
    while (std::getline(f, ln)) {
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == '\n')) ln.pop_back();
        if (ln.size() < 3 || ln[1] != '\t') continue;
        if (ln[0] == 'v') g_newsHistory.push_back({ln.substr(2), {}});
        else if (ln[0] == 'l' && !g_newsHistory.empty()) g_newsHistory.back().lines.push_back(ln.substr(2));
    }
    // Test builds are not releases: an older launcher recorded them here (v2.903.1.3 ...), so drop them on load.
    const std::size_t before = g_newsHistory.size();
    g_newsHistory.erase(std::remove_if(g_newsHistory.begin(), g_newsHistory.end(), [](const NewsRelease& r) {
        return okay::testbuild::IsTestVersion(r.ver.size() > 1 && r.ver[0] == 'v' ? r.ver.substr(1) : r.ver); }), g_newsHistory.end());
    if (g_newsHistory.size() != before) SaveNewsHistory();
}
// Record the latest fetched notes (GetNews) in the history. Cheap when nothing changed.
void SyncNewsHistory() {
    std::string ver, lines;
    GetNews(ver, lines);
    if (ver.empty() || lines.empty()) return;
    if (okay::testbuild::IsTestVersion(ver.size() > 1 && ver[0] == 'v' ? ver.substr(1) : ver)) return;   // test builds are not releases
    std::vector<std::string> ls;
    for (std::size_t s = 0; s < lines.size();) {
        std::size_t e = lines.find('\n', s); if (e == std::string::npos) e = lines.size();
        if (e > s) ls.push_back(lines.substr(s, e - s));
        s = e + 1;
    }
    for (auto& r : g_newsHistory) if (r.ver == ver) { if (r.lines != ls) { r.lines = ls; SaveNewsHistory(); } return; }
    g_newsHistory.insert(g_newsHistory.begin(), {ver, ls});
    std::stable_sort(g_newsHistory.begin(), g_newsHistory.end(), [](const NewsRelease& a, const NewsRelease& b) {
        auto num = [](const std::string& v) { return v.size() > 1 && v[0] == 'v' ? v.substr(1) : v; };
        return CompareVersions(num(a.ver), num(b.ver)) > 0; });
    if (g_newsHistory.size() > 12) g_newsHistory.resize(12);
    SaveNewsHistory();
}

} // namespace okl
