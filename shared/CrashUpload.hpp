#pragma once
// Sending crash reports (docs/crash_reports.md): the part of the launcher that, on its next look after a crash, turns each new report
// into one scrubbed row for the Supabase table `crash_reports` (docs/sql/crash_reports.sql), and the fallback that builds a prefilled
// "new issue" web address for a person to submit themselves.
//
// Pure logic, header only, standard C++17 and no threads or locks (CLAUDE.md rule 21a). The network is an injected Transport, the clock
// is a parameter, the files are read and written through two functions at the bottom, so tests/test_crash_upload.cpp runs every case
// (offline, 5xx, a table that is not there, a duplicate, a limit, a hostile report) without a server.
//
// What it guarantees:
//   * Nothing is sent unless sending is on (Enabled): the Settings switch is ON by default (owner's choice, 2026-11-15) and
//     OKAY_CRASH_UPLOAD=0 turns it off whatever the switch says.
//   * What is sent is the SCRUBBED report (shared/CrashScrub.hpp) plus a few columns taken from it, at most 100 KB, and never anything
//     but those fields (BuildPayload is the only place a body is made).
//   * A report is sent at most once: the ledger (okay_crash_sent.txt) records every id it sent, and a report the server refuses for good
//     (too big, malformed) is recorded too and never retried. If the ledger cannot be written, nothing is sent.
//   * At most 5 uploads in any 24 hours. Offline, a server error or a table that is not set up keeps the reports and tries again later,
//     with a growing wait (5 minutes up to 6 hours).
//   * Only the public anon key is used: the table accepts inserts from it and nothing else (the SQL says so; the tests there check it).
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "CrashReports.hpp"
#include "CrashScrub.hpp"

namespace okaycrash {
namespace upload {

constexpr std::size_t kMaxBodyBytes = 100 * 1024;   // the whole JSON body
constexpr int kMaxPerDay = 5;
constexpr long kMaxAgeSeconds = 30L * 24 * 3600;    // a report older than this is history, not news
constexpr const char* kTablePath = "/rest/v1/crash_reports";
constexpr const char* kLedgerName = "okay_crash_sent.txt";

// ---- Switch ---------------------------------------------------------------------------------------------------------------------------
// OKAY_CRASH_UPLOAD: "0", "false", "off" or "no" turns sending off whatever the Settings switch says. Any other value, or unset, leaves it to the switch.
inline bool EnvDisabled(const char* value) {
    if (!value) return false;
    std::string v = ScrubLower(value);
    while (!v.empty() && std::isspace((unsigned char)v.back())) v.pop_back();
    std::size_t i = 0; while (i < v.size() && std::isspace((unsigned char)v[i])) ++i;
    v = v.substr(i);
    return v == "0" || v == "false" || v == "off" || v == "no";
}
inline bool Enabled(bool settingOn, const char* envValue) { return settingOn && !EnvDisabled(envValue); }
// One plain sentence for the Settings page.
inline std::string StatusLine(bool settingOn, const char* envValue) {
    if (EnvDisabled(envValue)) return "Off: the environment variable OKAY_CRASH_UPLOAD is 0.";
    if (!settingOn) return "Off: you turned it off. Reports stay on this computer.";
    return "On: new crash reports are sent the next time the launcher runs.";
}
// What is sent, in the one line the Settings page shows next to the switch.
inline const char* DisclosureLine() {
    return "Sends: the editor version, your Windows version, CPU, memory and graphics card, which scene and mode it was in, the last 200 Console lines, "
           "the steps it took and the crash location. Your name, folders, email, computer name and IP address are removed first.";
}

// ---- Text helpers -------------------------------------------------------------------------------------------------------------------
inline std::string JsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7F) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    return o;
}
// Well-formed UTF-8 only: a stray byte would make the server refuse the whole row, so it becomes '?'.
inline std::string Utf8Clean(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = n != 0 && i + n <= s.size();
        for (std::size_t k = 1; ok && k < n; ++k) ok = ((unsigned char)s[i + k] & 0xC0) == 0x80;
        if (ok && n == 2 && c < 0xC2) ok = false;                                                  // overlong
        if (ok && n == 3) { const unsigned char c1 = (unsigned char)s[i + 1]; if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 >= 0xA0)) ok = false; }   // overlong, surrogates
        if (ok && n == 4) { const unsigned char c1 = (unsigned char)s[i + 1]; if (c > 0xF4 || (c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 >= 0x90)) ok = false; }
        if (ok) { o.append(s, i, n); i += n; }
        else { o += '?'; ++i; }
    }
    return o;
}
// A column value: scrubbed, one line, clean UTF-8, at most `cap` bytes (cut at a character boundary).
inline std::string Column(const std::string& raw, const ScrubContext& ctx, std::size_t cap) {
    std::string v = Utf8Clean(Scrub(raw, ctx));
    for (char& c : v) if (c == '\n' || c == '\t') c = ' ';
    v = Trim(v);
    if (v.size() > cap) {
        v.resize(cap);
        while (!v.empty() && ((unsigned char)v.back() & 0xC0) == 0x80) v.pop_back();   // not in the middle of a character
        if (!v.empty() && (unsigned char)v.back() >= 0xC0) v.pop_back();
    }
    return v;
}

// ---- The report as a row --------------------------------------------------------------------------------------------------------------
// The id a report goes by: the same text is the same id (so a re-read or a retry never sends twice).
inline std::string ReportId(const Report& r) { return HashHex(r.text); }

struct Payload {
    bool ok = false;
    std::string error;       // why not, when !ok
    std::string reportId, signature, version, build, os, exception, lastStep, topFrame;
    std::string report;      // the scrubbed text (what the server stores)
    std::string json;        // the request body
    bool trimmed = false;    // Console lines (or the end) were cut to fit the size limit
};

inline std::string BodyOf(const Payload& p) {
    std::string j = "{";
    auto add = [&](const char* k, const std::string& v, bool first = false) {
        if (!first) j += ",";
        j += std::string("\"") + k + "\":\"" + JsonEscape(v) + "\"";
    };
    add("report_id", p.reportId, true);
    add("signature", p.signature);
    add("version", p.version);
    add("build", p.build);
    add("os", p.os);
    add("exception", p.exception);
    add("last_step", p.lastStep);
    add("top_frame", p.topFrame);
    add("report", p.report);
    j += "}";
    return j;
}

// Cut Console lines (oldest first) from the report text until the body fits, then the tail of the text if it still does not.
inline void FitBody(Payload& p, std::size_t maxBytes) {
    p.json = BodyOf(p);
    if (p.json.size() <= maxBytes) return;
    p.trimmed = true;
    const std::string mark = "--- Console";
    const std::size_t cAt = p.report.find(mark);
    if (cAt != std::string::npos) {
        const std::size_t headEnd = p.report.find('\n', cAt);
        const std::size_t endAt = p.report.find("--- End of report ---", cAt);
        if (headEnd != std::string::npos && endAt != std::string::npos && headEnd < endAt) {
            const std::string head = p.report.substr(0, headEnd + 1), tail = p.report.substr(endAt);
            std::vector<std::string> lines;
            std::istringstream in(p.report.substr(headEnd + 1, endAt - headEnd - 1));
            for (std::string l; std::getline(in, l);) lines.push_back(l);
            std::size_t drop = 0;
            while (drop < lines.size()) {
                drop = std::min(lines.size(), drop + std::max<std::size_t>(1, lines.size() / 10));
                std::string body = head + "[the oldest " + std::to_string(drop) + " Console lines were left out to fit the size limit]\n";
                for (std::size_t i = drop; i < lines.size(); ++i) body += lines[i] + "\n";
                p.report = body + tail;
                p.json = BodyOf(p);
                if (p.json.size() <= maxBytes) return;
            }
        }
    }
    // Still too big (a huge backtrace or header): keep the start of the report.
    for (int guard = 0; guard < 40 && p.json.size() > maxBytes; ++guard) {
        const double ratio = (double)maxBytes / (double)p.json.size();
        std::size_t keep = (std::size_t)((double)p.report.size() * ratio * 0.9);
        if (keep >= p.report.size()) keep = p.report.size() - 1;
        p.report.resize(keep);
        while (!p.report.empty() && ((unsigned char)p.report.back() & 0xC0) == 0x80) p.report.pop_back();
        p.report += "\n--- cut: the report was too long ---\n";
        p.json = BodyOf(p);
    }
}

inline Payload BuildPayload(const Report& r, const ScrubContext& ctx, std::size_t maxBytes = kMaxBodyBytes) {
    Payload p;
    if (r.text.empty()) { p.error = "empty report"; return p; }
    p.reportId = ReportId(r);
    p.signature = r.signature.empty() ? Signature(r) : r.signature;   // from the original text: scrubbing would change the frames' names
    p.version = Column(r.version.empty() ? "unknown" : r.version, ctx, 40);
    p.build = Column(r.build, ctx, 120);
    p.os = Column(r.os, ctx, 200);
    p.exception = Column(r.exception.empty() ? "unknown" : r.exception, ctx, 120);
    p.lastStep = Column(r.lastStep, ctx, 200);
    p.topFrame = Column(TopFrame(r), ctx, 300);
    p.report = LimitLines(Utf8Clean(Scrub(r.text, ctx)));
    FitBody(p, maxBytes);
    if (p.json.size() > maxBytes) { p.error = "report too large"; return p; }
    p.ok = true;
    return p;
}

// What the "Preview what would be sent" view shows: where it goes, the columns, then the text, all as it will leave the machine.
inline std::string PreviewText(const Payload& p, const std::string& host) {
    std::string t;
    t += "Sent to: " + (host.empty() ? std::string("(no server set)") : host) + "  (table crash_reports, insert only)\n";
    t += "Size: " + std::to_string(p.json.size()) + " bytes";
    if (p.trimmed) t += " (older Console lines were left out to stay under 100 KB)";
    t += "\n\nColumns\n  report id:  " + p.reportId + "\n  signature:  " + p.signature + "\n  version:    " + p.version + "\n  build:      " + p.build +
         "\n  os:         " + p.os + "\n  exception:  " + p.exception + "\n  last step:  " + p.lastStep + "\n  top frame:  " + p.topFrame + "\n\nReport text\n";
    t += p.report;
    return t;
}

// ---- The ledger: what was sent, what was refused for good, when to try again --------------------------------------------------------
class Ledger {
public:
    bool Sent(const std::string& id) const { return sent_.count(id) != 0; }
    bool Refused(const std::string& id) const { return refused_.count(id) != 0; }
    void MarkSent(const std::string& id, long t) { sent_[id] = t; refused_.erase(id); }
    void MarkRefused(const std::string& id, long t) { refused_[id] = t; }
    int SentSince(long since) const { int n = 0; for (const auto& kv : sent_) if (kv.second >= since) ++n; return n; }
    long RetryAfter() const { return retryAfter_; }
    int Fails() const { return fails_; }
    void SetRetry(long notBefore, int fails) { retryAfter_ = notBefore; fails_ = fails; }
    std::size_t SentCount() const { return sent_.size(); }
    long LastSent() const { long t = 0; for (const auto& kv : sent_) t = std::max(t, kv.second); return t; }

    std::string Serialize() const {
        std::string s = "# crash reports this computer has sent or been told to keep (docs/crash_reports.md). Safe to delete: then old reports could be sent again.\n";
        for (const auto& kv : sent_) s += "sent " + kv.first + " " + std::to_string(kv.second) + "\n";
        for (const auto& kv : refused_) s += "refused " + kv.first + " " + std::to_string(kv.second) + "\n";
        if (retryAfter_ > 0 || fails_ > 0) s += "retry " + std::to_string(retryAfter_) + " " + std::to_string(fails_) + "\n";
        return s;
    }
    static Ledger Parse(const std::string& text) {
        Ledger l;
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::istringstream ls(line);
            std::string kind, id; long t = 0; long f = 0;
            ls >> kind;
            if (kind == "sent" && (ls >> id >> t) && Plausible(id)) l.sent_[id] = t;
            else if (kind == "refused" && (ls >> id >> t) && Plausible(id)) l.refused_[id] = t;
            else if (kind == "retry" && (ls >> t >> f)) { l.retryAfter_ = t; l.fails_ = (int)std::max(0L, std::min(f, 30L)); }
        }
        return l;
    }
private:
    static bool Plausible(const std::string& id) { return id.size() == 16 && id.find_first_not_of("0123456789abcdef") == std::string::npos; }
    std::map<std::string, long> sent_, refused_;
    long retryAfter_ = 0;
    int fails_ = 0;
};

inline bool LoadLedger(const std::string& path, Ledger& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { out = Ledger(); return true; }   // no file yet is a fresh ledger
    std::ostringstream ss; ss << f.rdbuf();
    out = Ledger::Parse(ss.str());
    return true;
}
// Writes through a temporary file, so a crash while saving never leaves half a ledger. False when it cannot be written.
inline bool SaveLedger(const std::string& path, const Ledger& l) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << l.Serialize();
        f.flush();
        if (!f) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) { std::filesystem::remove(path, ec); ec.clear(); std::filesystem::rename(tmp, path, ec); }   // (Windows does not replace in rename on every filesystem)
    return !ec;
}

// ---- The network, as the launcher sees it -------------------------------------------------------------------------------------------
struct Request { std::string method = "POST", path, header, body; };
struct Reply { bool reached = false; long status = 0; std::string body; };
using Transport = std::function<Reply(const Request&)>;

inline Request BuildRequest(const Payload& p) {
    Request r;
    r.path = kTablePath;
    r.header = "Prefer: return=minimal";   // the anon role may INSERT and nothing else, so the row is never read back
    r.body = p.json;
    return r;
}

enum class Outcome { Sent, Duplicate, NoNetwork, NotSetUp, Denied, Refused, Retry };

// 201 / 204: stored.  409: the same report id is already there (unique violation).  400 / 413 / 422: the server will never take this
// body (kept, not retried).  404 with PGRST205 / 42P01: the table is not set up yet.  401 / 403: this key may not insert.  429, 5xx,
// "crash_rate_limit" (the table's cap of 300 rows an hour) and anything else: try again later.
inline Outcome Classify(const Reply& r) {
    if (!r.reached || r.status == 0) return Outcome::NoNetwork;
    if (r.status == 200 || r.status == 201 || r.status == 204) return Outcome::Sent;
    const std::string low = ScrubLower(r.body.size() < 65536 ? r.body : r.body.substr(0, 65536));
    auto has = [&](const char* s) { return low.find(s) != std::string::npos; };
    if (has("crash_rate_limit")) return Outcome::Retry;   // the table's flood cap (docs/sql/crash_reports.sql): a 400, but only for now
    if (r.status == 409 || has("23505") || has("duplicate key")) return Outcome::Duplicate;
    if (r.status == 404 || has("pgrst205") || has("42p01") || has("could not find the table")) return Outcome::NotSetUp;
    if (r.status == 401 || r.status == 403 || has("row-level security") || has("42501") || has("jwt")) return Outcome::Denied;
    if (r.status == 400 || r.status == 413 || r.status == 422 || has("check constraint") || has("23514")) return Outcome::Refused;
    return Outcome::Retry;
}

// 5 minutes, doubling, never more than 6 hours.
inline long BackoffSeconds(int fails) {
    long s = 300;
    for (int i = 1; i < fails && s < 6 * 3600; ++i) s *= 2;
    return std::min(s, 6L * 3600);
}

// "2026-11-15 10:00:00" (the editor writes local time) -> seconds since 1970, read as UTC: good to within a day, which is all the age rule needs. 0 if unreadable.
inline long EpochFromWhen(const std::string& when) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(when.c_str(), "%4d-%2d-%2d %2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    y -= mo <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (unsigned)(mo + (mo > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return ((era * 146097 + (long)doe - 719468) * 86400) + h * 3600L + mi * 60L + s;
}

struct Options {
    long now = 0;                         // seconds since 1970
    int maxPerDay = kMaxPerDay;
    long maxAgeSeconds = kMaxAgeSeconds;
    std::size_t maxBytes = kMaxBodyBytes;
};

struct Result {
    int sent = 0;             // rows the server accepted now
    int duplicates = 0;       // the server already had them (counted as sent)
    int alreadySent = 0;      // skipped: in the ledger
    int tooOld = 0;
    int refused = 0;          // the server will never take these (now in the ledger)
    int limited = 0;          // left for tomorrow: 5 uploads in 24 hours
    int failed = 0;           // a server error: kept, retried later
    bool offline = false;
    bool notSetUp = false;    // the table is not there (the owner has not run the SQL)
    bool denied = false;
    bool waiting = false;     // the retry time from an earlier failure has not come
    bool changedLedger = false;
    std::vector<std::string> sentIds;
    std::string note;         // one plain sentence for the Settings page
};

// Reports that would be tried now (not sent, not refused, not too old). `reports` is newest first.
inline std::vector<const Report*> Pending(const std::vector<Report>& reports, const Ledger& ledger, const Options& o) {
    std::vector<const Report*> v;
    for (const Report& r : reports) {
        if (r.text.empty()) continue;
        const std::string id = ReportId(r);
        if (ledger.Sent(id) || ledger.Refused(id)) continue;
        const long t = EpochFromWhen(r.when);
        if (t > 0 && o.now > 0 && o.now - t > o.maxAgeSeconds) continue;
        v.push_back(&r);
    }
    return v;
}

// Send what is waiting: newest first, one request each, stopping at the first sign that the next would fail the same way.
inline Result Run(const std::vector<Report>& reports, Ledger& ledger, const Transport& send, const ScrubContext& ctx, const Options& o) {
    Result res;
    for (const Report& r : reports) {   // counts for the note
        if (r.text.empty()) continue;
        const std::string id = ReportId(r);
        if (ledger.Sent(id) || ledger.Refused(id)) { ++res.alreadySent; continue; }
        const long t = EpochFromWhen(r.when);
        if (t > 0 && o.now > 0 && o.now - t > o.maxAgeSeconds) ++res.tooOld;
    }
    const std::vector<const Report*> todo = Pending(reports, ledger, o);
    if (todo.empty()) { res.note = reports.empty() ? "No crash reports." : "Nothing new to send."; return res; }
    if (o.now > 0 && ledger.RetryAfter() > o.now) {
        res.waiting = true;
        res.note = "Will try again in " + std::to_string((ledger.RetryAfter() - o.now + 59) / 60) + " minutes.";
        return res;
    }
    for (std::size_t i = 0; i < todo.size(); ++i) {
        const Report& r = *todo[i];
        if (ledger.SentSince(o.now - 24 * 3600) >= o.maxPerDay) {
            res.limited = (int)(todo.size() - i);
            res.note = "Sent " + std::to_string(o.maxPerDay) + " reports in the last day; the rest wait until tomorrow.";
            break;
        }
        const Payload p = BuildPayload(r, ctx, o.maxBytes);
        if (!p.ok) {   // cannot be made small enough: keep it local, never retry
            ledger.MarkRefused(ReportId(r), o.now); res.changedLedger = true; ++res.refused;
            continue;
        }
        const Reply reply = send(BuildRequest(p));
        const Outcome out = Classify(reply);
        if (out == Outcome::Sent || out == Outcome::Duplicate) {
            ledger.MarkSent(p.reportId, o.now); ledger.SetRetry(0, 0); res.changedLedger = true;
            if (out == Outcome::Sent) ++res.sent; else ++res.duplicates;
            res.sentIds.push_back(p.reportId);
            continue;
        }
        res.changedLedger = true;
        if (out == Outcome::Refused) { ledger.MarkRefused(p.reportId, o.now); ++res.refused; continue; }
        if (out == Outcome::NoNetwork) {
            res.offline = true; ledger.SetRetry(o.now + 300, ledger.Fails());
            res.note = "Offline: the reports are kept and sent when the launcher can reach the server.";
        } else if (out == Outcome::NotSetUp) {
            res.notSetUp = true; ledger.SetRetry(o.now + 6 * 3600, ledger.Fails());
            res.note = "The server has no crash_reports table yet (the owner has to run docs/sql/crash_reports.sql). The reports are kept.";
        } else if (out == Outcome::Denied) {
            res.denied = true; ledger.SetRetry(o.now + 6 * 3600, ledger.Fails());
            res.note = "The server refused the key for crash reports. The reports are kept.";
        } else {
            ++res.failed; const int f = ledger.Fails() + 1;
            ledger.SetRetry(o.now + BackoffSeconds(f), f);
            res.note = "The server had a problem (HTTP " + std::to_string(reply.status) + "). The reports are kept and tried again later.";
        }
        break;
    }
    if (res.note.empty()) {
        if (res.sent + res.duplicates > 0) res.note = "Sent " + std::to_string(res.sent + res.duplicates) + " crash report" + (res.sent + res.duplicates == 1 ? "" : "s") + ". Thank you.";
        else if (res.refused > 0) res.note = "The server would not take " + std::to_string(res.refused) + " report" + (res.refused == 1 ? "" : "s") + "; they stay on this computer.";
    }
    return res;
}

// ---- Fallback: report it yourself on GitHub --------------------------------------------------------------------------------------
inline std::string PercentEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

// A title is plain text on GitHub (no markdown), but "@name" and "#12" still turn into links and pings in other places it is shown, so they go,
// with line breaks and control characters. Cut to `cap` bytes at a character boundary.
inline std::string TitleText(const std::string& s, std::size_t cap) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c < 0x20 || c == '@' || c == '#') continue;
        if (c == ' ' && (o.empty() || o.back() == ' ')) continue;
        o += (char)c;
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    if (o.size() > cap) { o.resize(cap); while (!o.empty() && ((unsigned char)o.back() & 0xC0) == 0x80) o.pop_back(); if (!o.empty() && (unsigned char)o.back() >= 0xC0) o.pop_back(); o += "..."; }
    return o;
}
// A value inside a table cell: one line in a code span (so @mentions, #links, HTML and markdown do nothing), no backtick or pipe that could end it.
inline std::string CodeCell(const std::string& s, std::size_t cap) {
    std::string t = TitleText(s, cap);
    for (char& c : t) if (c == '`') c = '\''; else if (c == '|') c = '/';
    return "`" + t + "`";
}

struct IssueDraft { std::string title, body; bool trimmed = false; };

// A readable GitHub issue from a report: a short title, the facts in a table, the backtrace and the last Console lines in code blocks,
// the signature as a hidden marker (the server-side script uses it to find the same crash), and a "how to reproduce" stub.
// `maxConsole` limits the Console lines kept (the caller lowers it until the web address fits).
inline IssueDraft MakeIssueDraft(const Report& original, const ScrubContext& ctx, std::size_t maxConsole = 30) {
    const Report r = ParseOne(Scrub(original.text, ctx));   // everything below is from the scrubbed text
    IssueDraft d;
    const std::string top = TopFrame(r);
    d.title = "Crash: " + TitleText(ExceptionName(r.exception), 40) + (top.empty() ? "" : " in " + TitleText(top, 60)) + " (v" + TitleText(r.version.empty() ? "?" : r.version, 20) + ")";
    if (d.title.size() > 110) d.title = d.title.substr(0, 107) + "...";
    std::string b;
    b += "<!-- okay-crash-signature: " + (original.signature.empty() ? Signature(original) : original.signature) + " -->\n";
    b += "Sent from the launcher's Crash reports page. The text was cleaned of names, folders and addresses first.\n\n";
    b += "| | |\n|---|---|\n";
    auto row = [&](const char* k, const std::string& v) { if (!v.empty()) b += std::string("| ") + k + " | " + CodeCell(v, 160) + " |\n"; };
    row("Version", r.version); row("Build", r.build); row("When", r.when); row("Last step", r.lastStep); row("Exception", r.exception);
    row("OS", r.os); row("CPU", r.Info("CPU")); row("RAM", r.Info("RAM")); row("GPU", r.Info("GPU")); row("UI backend", r.Info("UI backend"));
    row("Mode", r.Info("Mode")); row("Scene", r.Info("Scene")); row("Objects", r.Info("Objects")); row("Uptime", r.Info("Uptime"));
    row("Own code", r.Info("Own code"));
    auto fence = [](const std::vector<std::string>& lines, std::size_t from) {
        std::string body;
        for (std::size_t i = from; i < lines.size(); ++i) body += lines[i] + "\n";
        std::size_t longest = 0, run = 0;
        for (char c : body) { run = c == '`' ? run + 1 : 0; longest = std::max(longest, run); }
        const std::string f(std::max<std::size_t>(3, longest + 1), '`');
        return f + "text\n" + body + f + "\n";
    };
    b += "\n### Backtrace\n" + fence(r.backtrace, 0);
    if (!r.breadcrumbs.empty()) b += "\n### Last steps\n" + fence(r.breadcrumbs, r.breadcrumbs.size() > 12 ? r.breadcrumbs.size() - 12 : 0);
    if (!r.console.empty() && maxConsole == 0) d.trimmed = true;
    if (!r.console.empty() && maxConsole > 0) {
        const std::size_t from = r.console.size() > maxConsole ? r.console.size() - maxConsole : 0;
        if (from > 0) d.trimmed = true;
        b += "\n### Console (last " + std::to_string(r.console.size() - from) + " lines)\n" + fence(r.console, from);
    }
    b += "\n### How to reproduce\n_Please fill in: what you were doing, and whether it happens every time._\n\n1. \n2. \n3. \n";
    d.body = LimitLines(b, 400);
    return d;
}

// https://github.com/<owner>/<repo>/issues/new?title=...&body=...&labels=crash, no longer than `maxUrl` (GitHub refuses about 8 KB).
// Console lines are dropped first; then the report is cut. `repo` is "owner/name".
inline std::string IssueUrl(const std::string& repo, const Report& original, const ScrubContext& ctx, std::size_t maxUrl = 7000, bool* trimmed = nullptr) {
    const std::string base = "https://github.com/" + repo + "/issues/new?labels=crash&title=";
    std::size_t keep = 30;
    for (;;) {
        const IssueDraft d = MakeIssueDraft(original, ctx, keep);
        const std::string url = base + PercentEncode(d.title) + "&body=" + PercentEncode(d.body);
        if (url.size() <= maxUrl) { if (trimmed) *trimmed = d.trimmed; return url; }
        if (keep > 0) { keep = keep > 5 ? keep - 5 : 0; continue; }
        // No Console left and still too long: cut the body (the whole text is on the clipboard, the page says so).
        std::string body = d.body;
        const std::string tail = "\n\n(The report was cut to fit a web address. Paste the full text from your clipboard here.)";
        while (!body.empty() && (base + PercentEncode(d.title) + "&body=" + PercentEncode(body + tail)).size() > maxUrl) body.resize(body.size() * 9 / 10);
        if (trimmed) *trimmed = true;
        return base + PercentEncode(d.title) + "&body=" + PercentEncode(body + tail);
    }
}

} // namespace upload
} // namespace okaycrash
