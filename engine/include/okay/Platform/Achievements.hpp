#pragma once
// Cross-game achievements, stored per-user on this machine so BOTH a running game
// and the launcher can read them (Steam-style "achievements" list). Local-first:
// it works with no server at all. Optional Supabase cloud sync can be layered on
// top later (see docs/sql/social.sql for the table) — nothing here requires it.
//
// Header-only + dependency-free (just std + filesystem) so the engine, player and
// launcher all share one implementation and one file.
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <filesystem>

namespace okay {

struct Achievement {
    std::string id;       // stable key, e.g. "first_win"
    std::string label;    // display name, e.g. "First Victory"
    long        when = 0; // unix time unlocked
    int         points = 10;   // achievement points it's worth (5..100; games choose, default 10)
};

class Achievements {
public:
    // Per-user shared folder: %LOCALAPPDATA%\OkaySpace on Windows, ~/.okayspace else.
    static std::string Dir() {
#if defined(_WIN32)
        const char* base = std::getenv("LOCALAPPDATA");
        std::string d = base && base[0] ? std::string(base) + "\\OkaySpace" : std::string("OkaySpace");
#else
        const char* home = std::getenv("HOME");
        std::string d = home && home[0] ? std::string(home) + "/.okayspace" : std::string(".okayspace");
#endif
        std::error_code ec; std::filesystem::create_directories(d, ec);
        return d;
    }
    static std::string File() {
        return Dir() + "/achievements.txt";   // lines: id|label|unixtime|points
    }

    static constexpr int kDefaultPoints = 10;
    static int ClampPoints(int p) { return p < 5 ? 5 : p > 100 ? 100 : p; }
    // A label may end with " +NN" to set the points ("Beat the boss +50").
    static void SplitPoints(std::string& label, int& points) {
        const std::size_t sp = label.rfind(" +");
        if (sp == std::string::npos || sp + 2 >= label.size()) return;
        for (std::size_t i = sp + 2; i < label.size(); ++i) if (label[i] < '0' || label[i] > '9') return;
        points = ClampPoints(std::atoi(label.c_str() + sp + 2));
        label.erase(sp);
    }

    // Record an unlock. Returns true the FIRST time (so a game can pop a toast only
    // once). Idempotent: unlocking again just keeps the earliest time.
    static bool Unlock(const std::string& id, const std::string& labelIn = "", int points = 0) {
        if (id.empty()) return false;
        std::vector<Achievement> all = All();
        for (const auto& a : all) if (a.id == id) return false;   // already have it
        std::string label = labelIn; int pts = points > 0 ? ClampPoints(points) : kDefaultPoints;
        if (points <= 0) SplitPoints(label, pts);
        Achievement a; a.id = id; a.label = label.empty() ? id : label; a.when = (long)std::time(nullptr); a.points = pts;
        std::ofstream f(File(), std::ios::app);
        if (!f) return false;
        f << Escape(a.id) << '|' << Escape(a.label) << '|' << a.when << '|' << a.points << '\n';
        return true;
    }

    // Total achievement points and the profile level they give (Steam-style:
    // every level costs a bit more than the last).
    static int TotalPoints(const std::vector<Achievement>& list) {
        int t = 0; for (const auto& a : list) t += a.points; return t;
    }
    static int Level(int points) {
        int lvl = 0, need = 50;
        while (points >= need) { points -= need; ++lvl; need += 25; }
        return lvl;
    }
    // Progress (0..1) from this level to the next.
    static float LevelProgress(int points) {
        int need = 50;
        while (points >= need) { points -= need; need += 25; }
        return (float)points / (float)need;
    }

    static bool IsUnlocked(const std::string& id) {
        for (const auto& a : All()) if (a.id == id) return true;
        return false;
    }

    static std::vector<Achievement> All() {
        std::vector<Achievement> out;
        std::ifstream f(File());
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            std::size_t p1 = line.find('|');
            if (p1 == std::string::npos) continue;
            std::size_t p2 = line.find('|', p1 + 1);
            std::size_t p3 = p2 == std::string::npos ? std::string::npos : line.find('|', p2 + 1);
            Achievement a;
            a.id    = Unescape(line.substr(0, p1));
            a.label = Unescape(p2 == std::string::npos ? line.substr(p1 + 1) : line.substr(p1 + 1, p2 - p1 - 1));
            a.when  = p2 == std::string::npos ? 0 : std::atol(line.substr(p2 + 1).c_str());
            if (p3 != std::string::npos) a.points = ClampPoints(std::atoi(line.substr(p3 + 1).c_str()));
            if (!a.id.empty()) out.push_back(a);
        }
        return out;
    }

    static int Count() { return (int)All().size(); }

    // Wipe all unlocks (a "New Game" / reset).
    static void Reset() { std::error_code ec; std::filesystem::remove(File(), ec); }

private:
    // '|' and newlines are the record separators, so escape them in fields.
    static std::string Escape(const std::string& s) {
        std::string o; o.reserve(s.size());
        for (char c : s) { if (c == '|') o += "\\p"; else if (c == '\n' || c == '\r') o += ' '; else if (c == '\\') o += "\\\\"; else o += c; }
        return o;
    }
    static std::string Unescape(const std::string& s) {
        std::string o; o.reserve(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) { char n = s[++i]; o += (n == 'p') ? '|' : n; }
            else o += s[i];
        }
        return o;
    }
};

} // namespace okay
