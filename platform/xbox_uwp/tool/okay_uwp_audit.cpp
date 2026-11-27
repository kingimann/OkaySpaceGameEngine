// okay-uwp-audit: lists what okay_engine uses that a UWP app may not, and checks that the sources the UWP target compiles are clean.
//   okay-uwp-audit <repo root> [--all]   (default: only the files okay_engine_uwp compiles; --all: the whole okay_engine list)
// Reads engine/CMakeLists.txt (the source list) and platform/xbox_uwp/engine/uwp_exclude.txt ("path   # reason" per line), follows the
// #include closure of the compiled files and scans every file with XboxUwpAudit.hpp. Exit 0 = no forbidden API in the closure.
// Plain C++17, no engine link. UNVERIFIED as a statement about UWP (the forbidden list is from memory of Microsoft's pages).
#include "XboxUwpAudit.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using namespace okay::xbox;

static bool Read(const fs::path& p, std::string& out) { std::ifstream f(p, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true; }

int main(int argc, char** argv) {
    if (argc < 2) { std::fputs("usage: okay-uwp-audit <repo root> [--all]\n", stderr); return 2; }
    const fs::path root = argv[1]; const bool all = argc > 2 && std::string(argv[2]) == "--all";
    std::string cm; if (!Read(root / "engine/CMakeLists.txt", cm)) { std::fputs("cannot read engine/CMakeLists.txt\n", stderr); return 2; }
    std::vector<std::string> srcs = ParseEngineSourceList(cm);
    for (const std::string& s : ParseEngineSourceList(cm, "okay_anticheat")) srcs.push_back(s);   // OkayAntiCheat, linked into okay_engine
    std::set<std::string> excluded; std::map<std::string, std::string> why;
    std::string ex; if (Read(root / "platform/xbox_uwp/engine/uwp_exclude.txt", ex)) {
        std::istringstream in(ex); std::string line;
        while (std::getline(in, line)) { const auto h = line.find('#'); std::string r = h == std::string::npos ? "" : line.substr(h + 1); if (h != std::string::npos) line.resize(h);
            std::istringstream w(line); std::string p; if (w >> p) { excluded.insert(p); while (!r.empty() && r[0] == ' ') r.erase(0, 1); why[p] = r; } }
    }
    std::vector<std::string> compiled;
    for (const std::string& s : srcs) if (all || !excluded.count(s)) compiled.push_back(s);
    std::printf("engine sources: %zu, excluded from the UWP target: %zu, scanned: %zu\n", srcs.size(), srcs.size() - compiled.size(), compiled.size());
    for (const std::string& e : excluded) { bool listed = false; for (const std::string& s : srcs) if (s == e) listed = true; if (!listed) std::printf("note: uwp_exclude.txt names %s which is not in the engine source list\n", e.c_str()); }
    std::set<std::string> seen; std::vector<fs::path> queue; int findings = 0;
    for (const std::string& s : compiled) queue.push_back(root / "engine" / s);
    std::map<std::string, int> byCategory;
    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
        const fs::path p = queue[qi]; const std::string key = p.lexically_normal().generic_string();
        if (!seen.insert(key).second) continue;
        std::string text; if (!Read(p, text)) { std::printf("warning: cannot read %s\n", key.c_str()); continue; }
        std::vector<std::string> inc; const auto f = ScanSource(text, &inc);
        for (const AuditFinding& a : f) { std::printf("%s:%d: %s [%s] -> %s\n", key.c_str(), a.line, a.token.c_str(), a.category.c_str(), a.alternative.c_str()); ++findings; ++byCategory[a.category]; }
        for (const std::string& i : inc) {
            for (const fs::path& base : {p.parent_path(), root / "engine/include", root / "engine/src", root / "shared"}) { const fs::path c = base / i; std::error_code ec; if (fs::is_regular_file(c, ec)) { queue.push_back(c); break; } }
        }
    }
    std::printf("files in the include closure: %zu, findings: %d\n", seen.size(), findings);
    for (const auto& kv : byCategory) std::printf("  %-24s %d\n", kv.first.c_str(), kv.second);
    return findings == 0 ? 0 : 1;
}
