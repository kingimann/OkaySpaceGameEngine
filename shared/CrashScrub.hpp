#pragma once
// Takes the personal parts out of a crash report before it leaves the machine (docs/crash_reports.md, "What is scrubbed").
// Header only, no I/O except reading a few environment variables in LocalContext(); the tests give it hostile text.
//
// What goes, in this order:
//   1. User folders:  C:\Users\<name>\...  ->  %USERPROFILE%\...   /home/<name>/ and /Users/<name>/  ->  ~/   (a name inside a longer path keeps the
//      path and becomes <user>); \\SERVER\share  ->  \\<host>\share.
//   2. Web addresses with a name and password in them (https://name:pass@host).
//   3. Secrets: GitHub / Slack / Stripe / AWS / Google / Supabase style keys, JSON web tokens, "Bearer ...", "Authorization: ...",
//      and the value of password=, token=, secret=, api_key= ... ; long opaque strings (hex of 32+, letters and digits of 40+).
//   4. Email addresses.  5. IP addresses (127.x and 0.0.0.0 stay; they say nothing about the person).
//   6. The names this machine goes by: the login name and the computer name (whole words, 3+ letters, not common words), and the home folder.
// Scrub() is idempotent: scrubbing scrubbed text changes nothing. Everything is plain byte scanning (UTF-8 passes through untouched).
// Written from the shapes of these identifiers, not from any other project's rules.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace okaycrash {

struct ScrubContext {
    std::vector<std::string> usernames;   // login names ("alice")
    std::vector<std::string> hostnames;   // computer names ("ALICE-PC")
    std::vector<std::string> homeDirs;    // "C:\Users\alice", "/home/alice"
};

inline std::string ScrubLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// The names this machine goes by, from the environment (the launcher and the editor run as the same person on the same machine).
inline ScrubContext LocalContext() {
    ScrubContext c;
    auto add = [](std::vector<std::string>& v, const char* name) {
        const char* e = std::getenv(name);
        if (e && *e && std::find(v.begin(), v.end(), e) == v.end()) v.push_back(e);
    };
    add(c.usernames, "USERNAME"); add(c.usernames, "USER"); add(c.usernames, "LOGNAME");
    add(c.hostnames, "COMPUTERNAME"); add(c.hostnames, "HOSTNAME"); add(c.hostnames, "USERDOMAIN");
    add(c.homeDirs, "USERPROFILE"); add(c.homeDirs, "HOME");
    return c;
}

namespace scrub_detail {

inline bool WordCh(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }
inline bool IsSep(char c) { return c == '\\' || c == '/'; }
inline bool IsHex(char c) { return std::isxdigit((unsigned char)c) != 0; }

// An account name inside a path: up to a separator, a quote or bracket, a control character or two spaces in a row. Spaces inside are
// allowed ("John Smith"). Returns the end index (exclusive); `i` is the start.
inline std::size_t NameEnd(const std::string& s, std::size_t i) {
    std::size_t j = i;
    while (j < s.size()) {
        const unsigned char c = (unsigned char)s[j];
        if (c < 0x20 || IsSep((char)c) || std::strchr("\"'`<>|*?:,;()[]{}", (char)c) != nullptr) break;
        if (c == ' ' && j + 1 < s.size() && s[j + 1] == ' ') break;
        ++j;
    }
    while (j > i && s[j - 1] == ' ') --j;
    return j;
}

// True when `at` is the start of a path: start of the text or after a space, quote, bracket, '=' or ':'.
inline bool PathStart(const std::string& s, std::size_t at) {
    if (at == 0) return true;
    const char p = s[at - 1];
    return p == ' ' || p == '\t' || p == '\n' || p == '"' || p == '\'' || p == '(' || p == '[' || p == '=' || p == ':' || p == '`' || p == '<' || p == ',';
}

inline bool IEquals(const std::string& s, std::size_t at, const char* word) {
    const std::size_t n = std::char_traits<char>::length(word);
    if (at + n > s.size()) return false;
    for (std::size_t k = 0; k < n; ++k) if (std::tolower((unsigned char)s[at + k]) != std::tolower((unsigned char)word[k])) return false;
    return true;
}

// ---- 1. user folders --------------------------------------------------------------------------------------------------------------------
inline std::string UserFolders(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    std::size_t i = 0;
    while (i < in.size()) {
        // Windows: <drive>:<sep>+Users<sep>+<name>   or   Documents and Settings
        if (std::isalpha((unsigned char)in[i]) && i + 2 < in.size() && in[i + 1] == ':' && IsSep(in[i + 2]) && (i == 0 || !WordCh((unsigned char)in[i - 1]))) {
            std::size_t j = i + 2;
            while (j < in.size() && IsSep(in[j])) ++j;
            std::size_t after = 0;
            if (IEquals(in, j, "Users")) after = j + 5;
            else if (IEquals(in, j, "Documents and Settings")) after = j + 22;
            if (after && after < in.size() && IsSep(in[after])) {
                while (after < in.size() && IsSep(in[after])) ++after;
                const std::size_t end = NameEnd(in, after);
                if (end > after && in[after] != ' ') { out += "%USERPROFILE%"; i = end; continue; }
            }
        }
        // POSIX / macOS: /home/<name>  /Users/<name>
        if (in[i] == '/' && (IEquals(in, i + 1, "home/") || IEquals(in, i + 1, "Users/"))) {
            const std::size_t nameAt = i + (IEquals(in, i + 1, "home/") ? 6 : 7);
            const std::size_t end = NameEnd(in, nameAt);
            if (end > nameAt && in[nameAt] != ' ' && !(end - nameAt == 1 && in[nameAt] == '.')) {
                if (PathStart(in, i)) out += "~";
                else out += in.substr(i, nameAt - i) + "<user>";
                i = end; continue;
            }
        }
        // UNC: \\HOST\share  (not \\?\ or \\.\)
        if (in[i] == '\\' && i + 2 < in.size() && in[i + 1] == '\\' && (i == 0 || !(WordCh((unsigned char)in[i - 1]) || in[i - 1] == '%' || in[i - 1] == '\\' || in[i - 1] == ':' || in[i - 1] == '/'))) {
            std::size_t j = i + 2;
            const std::size_t hostAt = j;
            while (j < in.size() && (std::isalnum((unsigned char)in[j]) || in[j] == '.' || in[j] == '-' || in[j] == '_')) ++j;
            if (j > hostAt && j < in.size() && in[j] == '\\' && in.compare(hostAt, 1, "?") != 0 && in.compare(hostAt, 1, ".") != 0) {
                out += "\\\\<host>"; i = j; continue;
            }
        }
        out += in[i++];
    }
    return out;
}

// ---- 2. user:password@ in a web address -----------------------------------------------------------------------------------------------
inline std::string UrlCredentials(const std::string& in) {
    std::string out;
    std::size_t i = 0;
    while (i < in.size()) {
        const std::size_t at = in.find("://", i);
        if (at == std::string::npos) { out += in.substr(i); break; }
        out += in.substr(i, at + 3 - i);
        std::size_t j = at + 3;
        while (j < in.size() && in[j] != '@' && in[j] != '/' && in[j] != '?' && in[j] != '#' && !std::isspace((unsigned char)in[j])) ++j;
        if (j < in.size() && in[j] == '@' && j > at + 3) { out += "<redacted>@"; i = j + 1; }
        else i = at + 3;
    }
    return out;
}

// ---- 3. secrets ---------------------------------------------------------------------------------------------------------------------------
inline bool TokenCh(char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == '~' || c == '+' || c == '/' || c == '='; }

inline std::string KnownKeys(const std::string& in) {
    struct Pre { const char* prefix; std::size_t minTail; };
    static const Pre kPre[] = {{"github_pat_", 16}, {"ghp_", 16}, {"gho_", 16}, {"ghu_", 16}, {"ghs_", 16}, {"ghr_", 16}, {"glpat-", 16},
        {"sk-ant-", 16}, {"sk-", 20}, {"pk_live_", 16}, {"sk_live_", 16}, {"rk_live_", 16}, {"sb_publishable_", 10}, {"sb_secret_", 10},
        {"xoxb-", 10}, {"xoxp-", 10}, {"xoxa-", 10}, {"xoxr-", 10}, {"xoxs-", 10}, {"AKIA", 16}, {"ASIA", 16}, {"AIza", 30}, {"hf_", 20},
        {"npm_", 20}, {"shpat_", 16}};
    std::string out;
    std::size_t i = 0;
    while (i < in.size()) {
        bool done = false;
        if (i == 0 || !WordCh((unsigned char)in[i - 1])) {
            for (const Pre& p : kPre) {
                const std::size_t pl = std::char_traits<char>::length(p.prefix);
                if (in.compare(i, pl, p.prefix) != 0) continue;
                std::size_t j = i + pl;
                while (j < in.size() && (std::isalnum((unsigned char)in[j]) || in[j] == '_' || in[j] == '-')) ++j;
                if (j - (i + pl) >= p.minTail) { out += "<token>"; i = j; done = true; }
                break;
            }
        }
        if (!done) out += in[i++];
    }
    return out;
}

inline std::string Jwt(const std::string& in) {
    std::string out;
    std::size_t i = 0;
    auto b64 = [](char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '-'; };
    while (i < in.size()) {
        if (in.compare(i, 3, "eyJ") == 0 && (i == 0 || !WordCh((unsigned char)in[i - 1]))) {
            std::size_t j = i;
            while (j < in.size() && b64(in[j])) ++j;
            if (j < in.size() && in[j] == '.' && j - i >= 8) {
                std::size_t k = j + 1;
                while (k < in.size() && b64(in[k])) ++k;
                if (k < in.size() && in[k] == '.' && k - j >= 6) {
                    std::size_t m = k + 1;
                    while (m < in.size() && b64(in[m])) ++m;
                    out += "<token>"; i = m; continue;
                }
            }
        }
        out += in[i++];
    }
    return out;
}

// "Authorization: <rest of the line>", "Bearer <token>", "Basic <token>".
inline std::string AuthHeaders(const std::string& in) {
    std::string out;
    std::size_t i = 0;
    while (i < in.size()) {
        if ((i == 0 || !WordCh((unsigned char)in[i - 1])) && IEquals(in, i, "authorization")) {
            std::size_t j = i + 13;
            while (j < in.size() && (in[j] == ' ' || in[j] == '\t')) ++j;
            if (j < in.size() && (in[j] == ':' || in[j] == '=')) {
                ++j;
                std::size_t e = in.find('\n', j);
                if (e == std::string::npos) e = in.size();
                out += in.substr(i, j - i) + " <redacted>"; i = e; continue;
            }
        }
        if ((i == 0 || !WordCh((unsigned char)in[i - 1])) && (IEquals(in, i, "bearer ") || IEquals(in, i, "basic "))) {
            const std::size_t wl = IEquals(in, i, "bearer ") ? 7 : 6;
            std::size_t j = i + wl;
            while (j < in.size() && TokenCh(in[j])) ++j;
            if (j - (i + wl) >= 8) { out += in.substr(i, wl) + "<token>"; i = j; continue; }
        }
        out += in[i++];
    }
    return out;
}

// password=..., "token": "...", api_key: ... : the value is replaced, the name stays.
inline bool SecretKeyName(const std::string& lowered) {
    static const char* const kParts[] = {"apikey", "api_key", "api-key", "token", "secret", "passw", "pwd", "authorization", "credential", "private_key",
                                         "privatekey", "accesskey", "access_key", "session_key", "sessionkey"};
    for (const char* p : kParts) if (lowered.find(p) != std::string::npos) return true;
    return lowered == "auth" || (lowered.size() > 5 && (lowered.compare(lowered.size() - 5, 5, "_auth") == 0 || lowered.compare(lowered.size() - 5, 5, "-auth") == 0));
}
inline std::string SecretValues(const std::string& in) {
    std::string out;
    std::size_t i = 0;
    while (i < in.size()) {
        if (std::isalpha((unsigned char)in[i]) && (i == 0 || !(std::isalnum((unsigned char)in[i - 1]) || in[i - 1] == '_' || in[i - 1] == '-'))) {
            std::size_t j = i;
            while (j < in.size() && (std::isalnum((unsigned char)in[j]) || in[j] == '_' || in[j] == '-')) ++j;
            const std::string key = ScrubLower(in.substr(i, j - i));
            std::size_t k = j;
            if (k < in.size() && (in[k] == '"' || in[k] == '\'')) ++k;   // "token": ...
            while (k < in.size() && (in[k] == ' ' || in[k] == '\t')) ++k;
            if (SecretKeyName(key) && k < in.size() && (in[k] == '=' || in[k] == ':') && !(in[k] == ':' && k + 1 < in.size() && in[k + 1] == ':')) {
                ++k;
                while (k < in.size() && (in[k] == ' ' || in[k] == '\t')) ++k;
                char quote = 0;
                if (k < in.size() && (in[k] == '"' || in[k] == '\'')) quote = in[k++];
                std::size_t v = k;
                while (v < in.size() && in[v] != '\n' && in[v] != '\r' && (quote ? in[v] != quote : !(std::isspace((unsigned char)in[v]) || in[v] == '&' || in[v] == ',' || in[v] == ';' || in[v] == ')' || in[v] == '"' || in[v] == '\''))) ++v;
                if (v > k) { out += in.substr(i, k - i) + "<redacted>"; i = v; continue; }
            }
            out += in.substr(i, j - i); i = j; continue;
        }
        out += in[i++];
    }
    return out;
}

// Long opaque runs: 32+ hex digits (a hash or a key), or 40+ letters and digits with both cases and a digit. A run next to '_' is an identifier.
inline std::string OpaqueRuns(const std::string& in) {
    std::string out;
    std::size_t i = 0;
    while (i < in.size()) {
        if (std::isalnum((unsigned char)in[i]) && (i == 0 || (!WordCh((unsigned char)in[i - 1])))) {
            std::size_t j = i;
            bool hexOnly = true, digit = false, upper = false, lower = false;
            while (j < in.size() && std::isalnum((unsigned char)in[j])) {
                const char c = in[j];
                if (!IsHex(c)) hexOnly = false;
                if (std::isdigit((unsigned char)c)) digit = true; else if (std::isupper((unsigned char)c)) upper = true; else lower = true;
                ++j;
            }
            const bool boundary = j >= in.size() || !WordCh((unsigned char)in[j]);
            const std::size_t len = j - i;
            if (boundary && ((hexOnly && len >= 32) || (len >= 40 && digit && upper && lower))) { out += "<token>"; i = j; continue; }
            out += in.substr(i, j - i); i = j; continue;
        }
        out += in[i++];
    }
    return out;
}

// ---- 4. emails ----------------------------------------------------------------------------------------------------------------------------
inline std::string Emails(const std::string& in) {
    auto local = [](char c) { return std::isalnum((unsigned char)c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-'; };
    auto dom = [](char c) { return std::isalnum((unsigned char)c) || c == '.' || c == '-'; };
    std::string out;
    std::size_t i = 0;
    while (i < in.size()) {
        if (in[i] == '@') {
            std::size_t l = out.size();
            while (l > 0 && local(out[l - 1])) --l;
            std::size_t r = i + 1;
            while (r < in.size() && dom(in[r])) ++r;
            while (r > i + 1 && (in[r - 1] == '.' || in[r - 1] == '-')) --r;   // a sentence's full stop is not part of the address
            const std::string domain = in.substr(i + 1, r - i - 1);
            const std::size_t dot = domain.rfind('.');
            bool tld = dot != std::string::npos && domain.size() - dot - 1 >= 2;
            if (tld) for (std::size_t k = dot + 1; k < domain.size(); ++k) if (!std::isalpha((unsigned char)domain[k])) tld = false;
            if (tld && l < out.size() && domain.find("..") == std::string::npos && domain[0] != '.') {
                out.resize(l); out += "<email>"; i = r; continue;
            }
        }
        out += in[i++];
    }
    return out;
}

// ---- 5. IP addresses -----------------------------------------------------------------------------------------------------------------------
inline std::string Ips(const std::string& in) {
    std::string out;
    std::size_t i = 0;
    while (i < in.size()) {
        const char c = in[i];
        // IPv4
        if (std::isdigit((unsigned char)c) && (i == 0 || !(std::isalnum((unsigned char)in[i - 1]) || in[i - 1] == '.' || in[i - 1] == '_'))) {
            std::size_t j = i; int groups = 0; bool ok = true; int first = -1;
            std::size_t endOfFour = 0;
            while (j < in.size() && groups < 5) {
                std::size_t d = j; int v = 0;
                while (d < in.size() && std::isdigit((unsigned char)in[d]) && d - j < 4) { v = v * 10 + (in[d] - '0'); ++d; }
                if (d == j || d - j > 3 || v > 255) { ok = false; break; }
                if (groups == 0) first = v;
                ++groups; j = d;
                if (groups == 4) endOfFour = j;
                if (j < in.size() && in[j] == '.' && j + 1 < in.size() && std::isdigit((unsigned char)in[j + 1])) ++j; else break;
            }
            if (ok && groups == 4 && !(endOfFour < in.size() && (std::isalnum((unsigned char)in[endOfFour]) || in[endOfFour] == '_'))) {
                const std::string ip = in.substr(i, endOfFour - i);
                const bool harmless = first == 127 || ip == "0.0.0.0";
                out += harmless ? ip : std::string("<ip>"); i = endOfFour; continue;
            }
            if (ok && groups >= 4) {   // 5+ groups: a version number, not an address
                out += in.substr(i, j - i); i = j; continue;
            }
        }
        // IPv6 / MAC: a run of hex digits and colons with four or more colons, or "::" between hex digits
        if ((IsHex(c) || c == ':') && (i == 0 || !(WordCh((unsigned char)in[i - 1]) || in[i - 1] == ':'))) {
            std::size_t j = i; int colons = 0; bool dbl = false, hexAdj = false;
            while (j < in.size() && (IsHex(in[j]) || in[j] == ':')) {
                if (in[j] == ':') { ++colons; if (j + 1 < in.size() && in[j + 1] == ':') dbl = true; }
                ++j;
            }
            const bool boundary = j >= in.size() || !(WordCh((unsigned char)in[j]));
            for (std::size_t k = i; k < j; ++k) if (IsHex(in[k])) { hexAdj = true; break; }
            const std::size_t len = j - i;
            if (boundary && hexAdj && len >= 5 && (colons >= 4 || (dbl && colons >= 2 && len >= 6))) {
                out += "<ip>"; i = j; continue;
            }
            if (j > i) { out += in.substr(i, j - i); i = j; continue; }
        }
        out += in[i++];
    }
    return out;
}

// ---- 6. this machine's own names -----------------------------------------------------------------------------------------------------
inline bool CommonWord(const std::string& lowered) {
    static const char* const kCommon[] = {"user", "users", "admin", "administrator", "test", "main", "root", "guest", "default", "public", "game", "dev",
        "app", "server", "editor", "okayspace", "okay", "engine", "scene", "player", "build", "runner", "ubuntu", "debian", "nobody", "system",
        "windows", "home", "temp", "tmp", "localhost", "unknown", "none", "null", "true", "false", "name", "data", "project", "level", "script"};
    for (const char* w : kCommon) if (lowered == w) return true;
    return false;
}
inline std::string ReplaceWord(const std::string& in, const std::string& word, const char* with) {
    if (word.size() < 3 || CommonWord(ScrubLower(word))) return in;
    std::string out;
    std::size_t i = 0;
    while (i < in.size()) {
        if (IEquals(in, i, word.c_str()) && (i == 0 || !WordCh((unsigned char)in[i - 1])) && (i + word.size() >= in.size() || !WordCh((unsigned char)in[i + word.size()]))) {
            out += with; i += word.size();
        } else out += in[i++];
    }
    return out;
}
inline std::string ReplaceFolder(const std::string& in, const std::string& folder, const char* with) {
    if (folder.size() < 4) return in;
    std::string out;
    std::size_t i = 0;
    const std::string alt = [&] { std::string a = folder; for (char& c : a) c = c == '\\' ? '/' : c == '/' ? '\\' : c; return a; }();
    while (i < in.size()) {
        if (IEquals(in, i, folder.c_str())) { out += with; i += folder.size(); }
        else if (IEquals(in, i, alt.c_str())) { out += with; i += alt.size(); }
        else out += in[i++];
    }
    return out;
}

inline std::string StripControl(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) { const unsigned char u = (unsigned char)c; if (u >= 0x20 || c == '\n' || c == '\t') out += c; else if (c == '\r') {} }
    return out;
}

} // namespace scrub_detail

// The report text with the personal parts taken out.
inline std::string Scrub(const std::string& text, const ScrubContext& ctx = ScrubContext()) {
    using namespace scrub_detail;
    std::string s = StripControl(text);
    for (const std::string& h : ctx.homeDirs) s = ReplaceFolder(s, h, h.find(':') != std::string::npos || h.compare(0, 2, "\\\\") == 0 ? "%USERPROFILE%" : "~");
    s = UserFolders(s);
    s = UrlCredentials(s);
    s = AuthHeaders(s);
    s = KnownKeys(s);
    s = Jwt(s);
    s = SecretValues(s);
    s = Emails(s);
    s = Ips(s);
    s = OpaqueRuns(s);
    // Longest names first ("Alice Smith" before "alice", "ALICE-PC" before "alice"), so a short name never eats part of a longer one.
    struct Named { const std::string* name; const char* with; };
    std::vector<Named> names;
    for (const std::string& h : ctx.hostnames) names.push_back({&h, "<host>"});
    for (const std::string& u : ctx.usernames) names.push_back({&u, "<user>"});
    std::stable_sort(names.begin(), names.end(), [](const Named& a, const Named& b) { return a.name->size() > b.name->size(); });
    for (const Named& n : names) s = ReplaceWord(s, *n.name, n.with);
    return s;
}

// Lines longer than `maxLen` are cut (a hostile or runaway line cannot fill the report).
inline std::string LimitLines(const std::string& in, std::size_t maxLen = 600) {
    std::string out;
    out.reserve(in.size());
    std::size_t i = 0;
    while (i < in.size()) {
        std::size_t e = in.find('\n', i);
        const bool last = e == std::string::npos;
        if (last) e = in.size();
        if (e - i > maxLen) { out += in.substr(i, maxLen); out += "...[cut]"; }
        else out += in.substr(i, e - i);
        if (!last) out += '\n';
        i = e + 1;
    }
    return out;
}

} // namespace okaycrash
