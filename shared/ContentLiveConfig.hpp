#pragma once
// Live config: remote settings, balancing values and feature toggles with optional start and end times, read through one small API.
// Design note: docs/design/live_updates.md. A live config file (".okaylive", manifest kind `config`) is ordinary signed content: its
// SHA-256 is in the signed content manifest, so it is exactly as trusted as the manifest and needs no signature of its own (no new
// cryptography, no second key). It reaches the game through the same sync, staging and rollback as every other content file, and is
// read from the verified tree only (Overlay::Find). The validator (GameContentValidators.hpp) refuses a damaged one before it is visible.
//
// Format (UTF-8, '\n' line ends, TAB between fields; '#' lines and blank lines are allowed; anything else is refused whole):
//
//   okay-live 1
//   set <TAB> <key> <TAB> <value> <TAB> <from or -> <TAB> <until or ->
//
// key: [a-z0-9._-], 1..64. value: any text without TAB or control characters, at most 1024 bytes. from / until: Unix seconds (UTC),
// "from" inclusive, "until" exclusive; "-" = open. A line is active at time t when from <= t < until. When several lines of one key
// are active, the one with the LATEST "from" wins (a scheduled event beats the base value), and among equal "from" the later line.
//
// The engine's settings stay ONE system: the console variables (okay/Core/Cvar.hpp, feature map 49.4) had no remote source; ApplyTo
// is that source. The game hands it the Cvar registry, so remote values go through the cvars' own parsing, ranges and read-only and
// cheat flags; a key no cvar has is still readable here (Get, IsOn) for game code and Visual Script.
//
// Written for this project from the behaviour the owner asked for (2026-11-28). Header only, no threads, no clock: the caller passes
// "now", so every schedule is tested without waiting.
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <set>
#include <limits>
#include <string>
#include <vector>

#include "ContentManifest.hpp"

namespace okaycontent {

inline constexpr const char* kLiveMagicLine = "okay-live 1";
inline constexpr std::uint64_t kLiveOpen = std::numeric_limits<std::uint64_t>::max();

class LiveConfig {
public:
    struct Line {
        std::string key, value;
        std::uint64_t from = 0, until = kLiveOpen;   ///< [from, until)
    };

    /// Strict parse. False with a plain `why` leaves the config EMPTY (every Get then returns its fallback: the shipped behaviour).
    bool Parse(const std::string& text, std::string* why = nullptr) {
        m_lines.clear();
        auto fail = [&](const std::string& m) { if (why) *why = m; m_lines.clear(); return false; };
        if (text.size() > (1u << 20)) return fail("the live config is larger than 1 MB");
        if (text.empty() || text.back() != '\n') return fail("the live config must end with a newline");
        const std::vector<std::string> lines = detail::Split(text.substr(0, text.size() - 1), '\n');
        if (lines[0] != kLiveMagicLine) return fail("not an okay-live 1 file");
        for (std::size_t i = 1; i < lines.size(); ++i) {
            const std::string& l = lines[i];
            const std::string at = "line " + std::to_string(i + 1) + ": ";
            if (l.empty() || l[0] == '#') continue;
            for (unsigned char c : l) if ((c < 32 && c != '\t') || c == 127) return fail(at + "contains a control character");
            const std::vector<std::string> f = detail::Split(l, '\t');
            if (f.size() != 5 || f[0] != "set") return fail(at + "expected set<TAB>key<TAB>value<TAB>from<TAB>until");
            Line ln;
            ln.key = f[1];
            ln.value = f[2];
            if (!detail::IdOk(ln.key, 64)) return fail(at + "the key is not [a-z0-9._-] (1 to 64)");
            if (ln.value.size() > 1024) return fail(at + "the value is longer than 1024 bytes");
            if (f[3] != "-" && !detail::Digits(f[3], ln.from, 1ull << 53)) return fail(at + "'from' is not Unix seconds or -");
            if (f[4] != "-" && !detail::Digits(f[4], ln.until, 1ull << 53)) return fail(at + "'until' is not Unix seconds or -");
            if (ln.until <= ln.from) return fail(at + "'until' is not after 'from'");
            m_lines.push_back(std::move(ln));
        }
        return true;
    }
    void Clear() { m_lines.clear(); }
    bool Empty() const { return m_lines.empty(); }
    const std::vector<Line>& Lines() const { return m_lines; }

    /// The active line for `key` at `now`, or nullptr.
    const Line* Active(const std::string& key, std::uint64_t now) const {
        const Line* best = nullptr;
        for (const Line& l : m_lines)
            if (l.key == key && l.from <= now && now < l.until && (!best || l.from >= best->from)) best = &l;
        return best;
    }
    std::string Get(const std::string& key, std::uint64_t now, const std::string& fallback = std::string()) const {
        const Line* l = Active(key, now);
        return l ? l->value : fallback;
    }
    /// A toggle: "1", "true", "on", "yes" are on; "0", "false", "off", "no" are off; anything else (or no active line) is `fallback`.
    bool IsOn(const std::string& key, std::uint64_t now, bool fallback = false) const {
        const Line* l = Active(key, now);
        if (!l) return fallback;
        const std::string v = detail::Lower(l->value);
        if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
        if (v == "0" || v == "false" || v == "off" || v == "no") return false;
        return fallback;
    }
    long long GetInt(const std::string& key, std::uint64_t now, long long fallback) const {
        const Line* l = Active(key, now);
        if (!l || l->value.empty()) return fallback;
        char* end = nullptr;
        const long long v = std::strtoll(l->value.c_str(), &end, 10);
        return (end && *end == '\0') ? v : fallback;
    }
    double GetNumber(const std::string& key, std::uint64_t now, double fallback) const {
        const Line* l = Active(key, now);
        if (!l || l->value.empty()) return fallback;
        char* end = nullptr;
        const double v = std::strtod(l->value.c_str(), &end);
        return (end && *end == '\0') ? v : fallback;
    }
    /// The first time after `now` at which some answer may change (a line starts or ends), or kLiveOpen when none will. The game can
    /// wait until then instead of asking every frame.
    std::uint64_t NextChange(std::uint64_t now) const {
        std::uint64_t next = kLiveOpen;
        for (const Line& l : m_lines) {
            if (l.from > now && l.from < next) next = l.from;
            if (l.until > now && l.until != kLiveOpen && l.until < next) next = l.until;
        }
        return next;
    }

    /// Hand each key's active value at `now` to `set` (key, value), once per key, in key order. Returns how many `set` accepted.
    /// The game passes the Cvar registry: [](const std::string& k, const std::string& v) { okay::Cvar* c = okay::Cvars::Find(k); return c && c->Set(v); }
    int ApplyTo(std::uint64_t now, const std::function<bool(const std::string&, const std::string&)>& set) const {
        std::set<std::string> keys;
        for (const Line& l : m_lines) keys.insert(l.key);
        int n = 0;
        for (const std::string& k : keys) if (const Line* l = Active(k, now)) if (set && set(k, l->value)) ++n;
        return n;
    }

    /// The one shared live config of the running game (loaded by the game from the verified content; read-only after that).
    static LiveConfig& Global() { static LiveConfig c; return c; }

private:
    std::vector<Line> m_lines;
};

/// Why a live config file is refused ("" = fine): the content validator's check, before the file is visible.
inline std::string LiveConfigProblem(const std::string& text) {
    LiveConfig c;
    std::string why;
    return c.Parse(text, &why) ? std::string() : why;
}

}  // namespace okaycontent
