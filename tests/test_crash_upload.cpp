#include "test_framework.hpp"
#include "CrashUpload.hpp"
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

// The crash report uploader (shared/CrashUpload.hpp) against a fake server: what is sent, when, how often, and what happens when the
// network or the server misbehaves. Every case runs without a network or a clock.
using namespace okaycrash;
using namespace okaycrash::upload;
namespace fs = std::filesystem;

static ScrubContext Alice() {
    ScrubContext c;
    c.usernames = {"alice"}; c.hostnames = {"ALICE-PC"}; c.homeDirs = {"C:\\Users\\alice"};
    return c;
}

// A report like the handler writes, `n` makes it a different crash (another time and another top frame).
static Report MakeReport(int n, const std::string& when = "2026-11-15 10:00:00", int consoleLines = 5) {
    std::string t =
        "OkaySpace crash report\nVersion: 2.874.0\nBuild: pe-6553A1B2-5F2000, built Nov 15 2026\nWhen: " + when + "\n"
        "Exe: C:\\Users\\alice\\AppData\\Local\\OkaySpace\\Tools\\OkayEngine.exe\nLast step: Render3D:gl\nException: 0xC0000005\n"
        "Fault module: OkayEngine.exe + 0x1A2B3C\nOS: Windows 10.0.19045 (x64)\nCPU: Test CPU (8 logical)\nGPU: Test GPU | Vendor | OpenGL 4.6\n"
        "Mode: edit\nScene: level1.okayscene\n\nBacktrace:\n  #00 OkayEngine.exe + 0x1A2B3C  DrawThing" + std::to_string(n) + " +0x40\n  #01 OkayEngine.exe + 0x22  main +0x9\n\n"
        "--- Breadcrumbs (oldest first: frame, step) ---\nframe 3: UI.Inspector\nframe 4: Render3D:gl\n"
        "--- Console (last " + std::to_string(consoleLines) + " lines, oldest first: seconds before the crash, level) ---\n";
    for (int i = 0; i < consoleLines; ++i) t += "[-" + std::to_string(consoleLines - i) + "s] I Loaded C:\\Users\\alice\\assets\\tex" + std::to_string(i) + ".png for alice@example.com\n";
    t += "--- End of report ---\n";
    return ParseOne(t);
}

// A tiny strict reader for the flat {"k":"v",...} body, to check the JSON round-trips.
static bool ReadBody(const std::string& j, std::map<std::string, std::string>& out) {
    std::size_t i = 0;
    auto ws = [&] { while (i < j.size() && (j[i] == ' ' || j[i] == '\n')) ++i; };
    auto str = [&](std::string& s) {
        if (i >= j.size() || j[i] != '"') return false;
        ++i; s.clear();
        while (i < j.size() && j[i] != '"') {
            const unsigned char c = (unsigned char)j[i];
            if (c < 0x20) return false;   // a raw control character is invalid JSON
            if (c == '\\') {
                if (++i >= j.size()) return false;
                const char e = j[i++];
                if (e == 'n') s += '\n'; else if (e == 'r') s += '\r'; else if (e == 't') s += '\t'; else if (e == '"') s += '"'; else if (e == '\\') s += '\\';
                else if (e == 'u') { unsigned v = 0; if (i + 4 > j.size() || std::sscanf(j.substr(i, 4).c_str(), "%4x", &v) != 1) return false; s += (char)v; i += 4; }
                else return false;
            } else { s += (char)c; ++i; }
        }
        if (i >= j.size()) return false;
        ++i; return true;
    };
    ws(); if (i >= j.size() || j[i] != '{') return false; ++i;
    for (;;) {
        ws(); std::string k, v;
        if (!str(k)) return false;
        ws(); if (i >= j.size() || j[i] != ':') return false; ++i; ws();
        if (!str(v)) return false;
        out[k] = v; ws();
        if (i < j.size() && j[i] == ',') { ++i; continue; }
        break;
    }
    ws(); return i + 1 == j.size() && j[i] == '}';
}

// The fake server.
struct Server {
    std::vector<Request> got;
    long status = 201;
    std::string body;
    bool reachable = true;
    Transport Transport_() {
        return [this](const Request& r) { got.push_back(r); Reply p; p.reached = reachable; p.status = reachable ? status : 0; p.body = body; return p; };
    }
};

int main() {
    RUN_SUITE("crash_upload");

    // ---- The switch ---------------------------------------------------------------------------------------------------------------------
    CHECK(!EnvDisabled(nullptr) && !EnvDisabled("") && !EnvDisabled("1") && !EnvDisabled("yes") && !EnvDisabled("on"));
    CHECK(EnvDisabled("0") && EnvDisabled("false") && EnvDisabled("FALSE") && EnvDisabled(" Off ") && EnvDisabled("no") && EnvDisabled("0\n"));
    CHECK(Enabled(true, nullptr) && Enabled(true, "1") && !Enabled(false, nullptr) && !Enabled(true, "0") && !Enabled(false, "1"));   // the variable can only turn it off
    CHECK(StatusLine(true, nullptr).find("On:") == 0 && StatusLine(false, nullptr).find("turned it off") != std::string::npos && StatusLine(true, "0").find("OKAY_CRASH_UPLOAD") != std::string::npos);
    CHECK(std::string(DisclosureLine()).find("last 200 Console lines") != std::string::npos);

    // ---- What a row holds ---------------------------------------------------------------------------------------------------------------
    {
        const Report r = MakeReport(1);
        const Payload p = BuildPayload(r, Alice());
        CHECK(p.ok && p.reportId.size() == 16 && p.signature == r.signature && p.signature.size() == 16);
        CHECK(p.version == "2.874.0" && p.build.find("pe-6553A1B2") == 0 && p.os == "Windows 10.0.19045 (x64)" && p.exception == "0xC0000005");
        CHECK(p.lastStep == "Render3D:gl" && p.topFrame == "OkayEngine.exe!DrawThing1");
        CHECK(p.json.find("alice") == std::string::npos && p.json.find("C:\\\\Users") == std::string::npos);                       // nothing personal in the body
        CHECK(p.report.find("%USERPROFILE%\\assets\\tex2.png for <email>") != std::string::npos && p.report.find("Exe: %USERPROFILE%\\AppData") != std::string::npos);
        std::map<std::string, std::string> back;
        CHECK(ReadBody(p.json, back) && back.size() == 9 && back["report"] == p.report && back["signature"] == p.signature && back["report_id"] == p.reportId);
        CHECK(back.count("report_id") && back.count("signature") && back.count("version") && back.count("build") && back.count("os") && back.count("exception") &&
              back.count("last_step") && back.count("top_frame") && back.count("report"));   // exactly the columns the SQL grants insert on
        // The request: anon insert, no read-back, nothing else in it.
        const Request q = BuildRequest(p);
        CHECK(q.method == "POST" && q.path == "/rest/v1/crash_reports" && q.header == "Prefer: return=minimal" && q.body == p.json);
        // The same report is the same id; another report is another.
        CHECK(BuildPayload(MakeReport(1), Alice()).reportId == p.reportId && BuildPayload(MakeReport(2), Alice()).reportId != p.reportId);
        CHECK(PreviewText(p, "x.supabase.co").find("Sent to: x.supabase.co") == 0 && PreviewText(p, "").find("(no server set)") != std::string::npos);
        CHECK(PreviewText(p, "h").find(p.report) != std::string::npos && PreviewText(p, "h").find("alice") == std::string::npos);
    }
    {   // an empty report is never sent
        CHECK(!BuildPayload(Report(), Alice()).ok);
    }
    {   // hostile text: quotes, backslashes, control characters, a stray byte, markdown, a huge line
        Report r = MakeReport(3);
        r.text += "\nhostile: \"quote\" \\ back \x01\x02 ctrl \xC3\x28 bad \xF0\x28\x8C\x28 utf8 <script>alert(1)</script> [x](javascript:1) @everyone\n";
        r.text += std::string(50000, 'z') + "\n";
        r.exception = "0xC0000005\" , \"injected\": \"yes";
        r.lastStep = std::string(5000, 's');
        r.version = "1.0\nsecond line";
        const Payload p = BuildPayload(r, Alice());
        CHECK(p.ok);
        std::map<std::string, std::string> back;
        CHECK(ReadBody(p.json, back));                                                    // still valid JSON, no raw control character in it
        CHECK(back.size() == 9 && back.count("injected") == 0);                           // the quote in the exception did not add a field
        CHECK(back["exception"] == "0xC0000005\" , \"injected\": \"yes");                 // it is just text
        CHECK(back["last_step"].size() == 200 && back["version"] == "1.0 second line");   // the columns are capped and one line
        CHECK(back["report"].find("\xC3\x28") == std::string::npos && back["report"].find("bad ?(") != std::string::npos);   // stray bytes became '?'
        CHECK(back["report"].find("zzzz...[cut]") != std::string::npos);                  // the huge line is cut
    }

    // ---- The size limit: 100 KB, whatever the report holds ------------------------------------------------------------------------------
    {
        const Report big = MakeReport(4, "2026-11-15 10:00:00", 2000);   // 2000 Console lines, over the limit once escaped
        const Payload p = BuildPayload(big, Alice(), kMaxBodyBytes);
        CHECK(p.ok && p.json.size() <= kMaxBodyBytes && p.trimmed);
        CHECK(p.report.find("oldest ") != std::string::npos && p.report.find("Console lines were left out") != std::string::npos);
        CHECK(p.report.find("tex1999.png") != std::string::npos);                    // the newest lines are the ones kept
        CHECK(p.report.find("Backtrace:") != std::string::npos && p.report.find("--- End of report ---") != std::string::npos);
        std::map<std::string, std::string> back;
        CHECK(ReadBody(p.json, back));
        Report huge = MakeReport(5);
        huge.text += std::string(900 * 1024, 'q');   // a megabyte of nothing but a header: no Console to cut, the end is cut
        for (std::size_t at = 0; at < huge.text.size(); at += 400) huge.text[at] = '\n';
        const Payload h = BuildPayload(huge, Alice(), kMaxBodyBytes);
        CHECK(h.ok && h.json.size() <= kMaxBodyBytes && h.trimmed);
        CHECK(!BuildPayload(MakeReport(6), Alice(), 300).ok);   // cannot be made that small: refused, not sent in pieces
    }

    // ---- The ledger -----------------------------------------------------------------------------------------------------------------------
    {
        Ledger l;
        l.MarkSent("00000000000000aa", 1000); l.MarkSent("00000000000000bb", 5000); l.MarkRefused("00000000000000cc", 2000); l.SetRetry(9000, 3);
        const Ledger b = Ledger::Parse(l.Serialize());
        CHECK(b.Sent("00000000000000aa") && b.Sent("00000000000000bb") && !b.Sent("00000000000000cc") && b.Refused("00000000000000cc"));
        CHECK(b.SentSince(4000) == 1 && b.SentSince(0) == 2 && b.RetryAfter() == 9000 && b.Fails() == 3 && b.SentCount() == 2 && b.LastSent() == 5000);
        // Hostile ledger text: junk lines, bad ids, huge numbers: ignored, never a crash, never a "sent" that was not.
        const Ledger g = Ledger::Parse("garbage\nsent\nsent nothex 5\nsent 00000000000000dd\nsent 00000000000000dd abc\nrefused ../../etc/passwd 1\nretry x y\n\nsent 00000000000000EE 1\nretry 5 9999\n");
        CHECK(!g.Sent("00000000000000dd") && !g.Sent("00000000000000EE") && g.SentCount() == 0 && g.RetryAfter() == 5 && g.Fails() == 30);   // (an upper-case id is not one of ours)
        CHECK(Ledger::Parse("").SentCount() == 0 && !Ledger::Parse("sent 00000000000000aa 1\r\n").Sent("00000000000000bb") && Ledger::Parse("sent 00000000000000aa 1\r\n").Sent("00000000000000aa"));
        // On disk.
        const fs::path dir = fs::temp_directory_path() / "okay_crash_upload_test";
        fs::remove_all(dir); fs::create_directories(dir);
        Ledger d; CHECK(LoadLedger((dir / "okay_crash_sent.txt").string(), d) && d.SentCount() == 0);   // none yet = empty
        d.MarkSent("00000000000000aa", 7);
        CHECK(SaveLedger((dir / "okay_crash_sent.txt").string(), d));
        Ledger e; CHECK(LoadLedger((dir / "okay_crash_sent.txt").string(), e) && e.Sent("00000000000000aa"));
        d.MarkSent("00000000000000bb", 8);
        CHECK(SaveLedger((dir / "okay_crash_sent.txt").string(), d));                                   // overwriting works
        Ledger e2; LoadLedger((dir / "okay_crash_sent.txt").string(), e2);
        CHECK(e2.SentCount() == 2 && !fs::exists(dir / "okay_crash_sent.txt.tmp"));
        CHECK(!SaveLedger((dir / "no_such_folder" / "x.txt").string(), d));                             // cannot be written: false (the caller then sends nothing)
        fs::remove_all(dir);
    }

    // ---- Classifying the server's answer ------------------------------------------------------------------------------------------------
    {
        auto c = [](bool reached, long st, const std::string& body = "") { Reply r; r.reached = reached; r.status = st; r.body = body; return Classify(r); };
        CHECK(c(true, 201) == Outcome::Sent && c(true, 204) == Outcome::Sent && c(true, 200) == Outcome::Sent);
        CHECK(c(false, 0) == Outcome::NoNetwork && c(true, 0) == Outcome::NoNetwork);
        CHECK(c(true, 409, "{\"code\":\"23505\"}") == Outcome::Duplicate);
        CHECK(c(true, 404, "{\"code\":\"PGRST205\",\"message\":\"Could not find the table 'public.crash_reports'\"}") == Outcome::NotSetUp);
        CHECK(c(true, 400, "{\"code\":\"42P01\"}") == Outcome::NotSetUp);
        CHECK(c(true, 401) == Outcome::Denied && c(true, 403, "new row violates row-level security policy") == Outcome::Denied);
        CHECK(c(true, 400, "{\"code\":\"23514\",\"message\":\"violates check constraint\"}") == Outcome::Refused && c(true, 413) == Outcome::Refused && c(true, 422) == Outcome::Refused);
        CHECK(c(true, 400, "{\"code\":\"P0001\",\"message\":\"crash_rate_limit\"}") == Outcome::Retry);   // the flood cap is a 400, but only for now
        CHECK(c(true, 429) == Outcome::Retry && c(true, 500) == Outcome::Retry && c(true, 503, "upstream") == Outcome::Retry && c(true, 302) == Outcome::Retry);
        CHECK(BackoffSeconds(1) == 300 && BackoffSeconds(2) == 600 && BackoffSeconds(3) == 1200 && BackoffSeconds(30) == 6 * 3600 && BackoffSeconds(0) == 300);
        CHECK(EpochFromWhen("1970-01-02 00:00:01") == 86401 && EpochFromWhen("2026-11-15 10:00:00") == 1794736800 && EpochFromWhen("nonsense") == 0 && EpochFromWhen("2026-13-01 00:00:00") == 0);
    }

    // ---- Running it ---------------------------------------------------------------------------------------------------------------------
    const long kNow = EpochFromWhen("2026-11-15 12:00:00");
    auto opts = [&](long now) { Options o; o.now = now; return o; };
    {   // two new reports: both sent, newest first, once; the next run sends nothing
        std::vector<Report> reports = {MakeReport(2, "2026-11-15 11:00:00"), MakeReport(1, "2026-11-15 10:00:00")};
        Server srv; Ledger ledger;
        Result r = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow));
        CHECK(r.sent == 2 && r.failed == 0 && !r.offline && srv.got.size() == 2 && r.changedLedger && r.sentIds.size() == 2);
        CHECK(srv.got[0].body.find("DrawThing2") != std::string::npos && srv.got[1].body.find("DrawThing1") != std::string::npos);   // newest first
        CHECK(srv.got[0].path == "/rest/v1/crash_reports" && srv.got[0].header == "Prefer: return=minimal" && srv.got[0].method == "POST");
        CHECK(r.note.find("Sent 2 crash reports") == 0 && ledger.SentCount() == 2);
        Result again = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow + 60));
        CHECK(again.sent == 0 && srv.got.size() == 2 && !again.changedLedger && again.alreadySent == 2 && again.note == "Nothing new to send.");   // never twice
        // A saved and reloaded ledger still stops a resend (the launcher restarted).
        Ledger reloaded = Ledger::Parse(ledger.Serialize());
        CHECK(Run(reports, reloaded, srv.Transport_(), Alice(), opts(kNow + 3600)).sent == 0 && srv.got.size() == 2);
        // A new crash after that is sent alone.
        reports.insert(reports.begin(), MakeReport(3, "2026-11-15 12:30:00"));
        CHECK(Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow + 3600)).sent == 1 && srv.got.size() == 3);
    }
    {   // nothing to do: the network is not touched
        Server srv; Ledger ledger;
        CHECK(Run({}, ledger, srv.Transport_(), Alice(), opts(kNow)).note == "No crash reports." && srv.got.empty());
    }
    {   // at most 5 in 24 hours
        std::vector<Report> reports;
        for (int i = 0; i < 8; ++i) reports.push_back(MakeReport(100 + i, "2026-11-15 11:0" + std::to_string(8 - i) + ":00"));
        Server srv; Ledger ledger;
        Result r = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow));
        CHECK(r.sent == 5 && r.limited == 3 && srv.got.size() == 5 && r.note.find("until tomorrow") != std::string::npos);
        Result r2 = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow + 3600));   // an hour later: still the limit
        CHECK(r2.sent == 0 && r2.limited == 3 && srv.got.size() == 5);
        Result r3 = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow + 24 * 3600 + 5));   // a day later: the rest
        CHECK(r3.sent == 3 && srv.got.size() == 8 && r3.limited == 0);
    }
    {   // offline: kept, retried after 5 minutes, then sent
        std::vector<Report> reports = {MakeReport(1)};
        Server srv; srv.reachable = false; Ledger ledger;
        Result r = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow));
        CHECK(r.offline && r.sent == 0 && srv.got.size() == 1 && ledger.SentCount() == 0 && ledger.RetryAfter() == kNow + 300 && r.note.find("Offline") == 0);
        Result wait = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow + 120));
        CHECK(wait.waiting && srv.got.size() == 1 && wait.note.find("3 minutes") != std::string::npos);   // not asked again before the time
        srv.reachable = true;
        Result later = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow + 301));
        CHECK(later.sent == 1 && srv.got.size() == 2 && ledger.RetryAfter() == 0);
    }
    {   // a server error: grows the wait, then recovers
        std::vector<Report> reports = {MakeReport(1)};
        Server srv; srv.status = 503; Ledger ledger;
        Result r1 = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow));
        CHECK(r1.failed == 1 && ledger.Fails() == 1 && ledger.RetryAfter() == kNow + 300 && r1.note.find("HTTP 503") != std::string::npos);
        Result r2 = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow + 301));
        CHECK(r2.failed == 1 && ledger.Fails() == 2 && ledger.RetryAfter() == kNow + 301 + 600);
        srv.status = 201;
        CHECK(Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow + 301 + 601)).sent == 1 && ledger.Fails() == 0);
    }
    {   // the table is not set up: kept, tried again in 6 hours, says why
        std::vector<Report> reports = {MakeReport(1)};
        Server srv; srv.status = 404; srv.body = "{\"code\":\"PGRST205\"}"; Ledger ledger;
        Result r = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow));
        CHECK(r.notSetUp && r.sent == 0 && ledger.RetryAfter() == kNow + 6 * 3600 && r.note.find("docs/sql/crash_reports.sql") != std::string::npos && ledger.SentCount() == 0);
        // The server stops sending the table later: the same report goes then.
        srv.status = 201; srv.body.clear();
        CHECK(Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow + 6 * 3600 + 1)).sent == 1);
    }
    {   // the key may not insert
        Server srv; srv.status = 403; srv.body = "new row violates row-level security policy"; Ledger ledger;
        Result r = Run({MakeReport(1)}, ledger, srv.Transport_(), Alice(), opts(kNow));
        CHECK(r.denied && ledger.SentCount() == 0 && ledger.RetryAfter() == kNow + 6 * 3600);
    }
    {   // a duplicate row on the server counts as sent (and is never sent again)
        Server srv; srv.status = 409; srv.body = "{\"code\":\"23505\"}"; Ledger ledger;
        Result r = Run({MakeReport(1)}, ledger, srv.Transport_(), Alice(), opts(kNow));
        CHECK(r.duplicates == 1 && r.sent == 0 && ledger.SentCount() == 1 && r.sentIds.size() == 1);
    }
    {   // the server refuses a report for good: never retried, and the next one still goes
        std::vector<Report> reports = {MakeReport(2, "2026-11-15 11:00:00"), MakeReport(1, "2026-11-15 10:00:00")};
        Server srv; srv.status = 400; srv.body = "{\"code\":\"23514\"}"; Ledger ledger;
        Result r = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow));
        CHECK(r.refused == 2 && r.sent == 0 && srv.got.size() == 2 && !ledger.Sent(BuildPayload(reports[0], Alice()).reportId) && ledger.Refused(BuildPayload(reports[0], Alice()).reportId));
        Result again = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow + 3600));
        CHECK(again.sent == 0 && srv.got.size() == 2);
    }
    {   // a report too big to fit never reaches the network
        Server srv; Ledger ledger; Options o = opts(kNow); o.maxBytes = 300;
        Result r = Run({MakeReport(1)}, ledger, srv.Transport_(), Alice(), o);
        CHECK(r.refused == 1 && srv.got.empty());
    }
    {   // a report older than 30 days is history, and an unreadable date is treated as new
        Server srv; Ledger ledger;
        std::vector<Report> reports = {MakeReport(1, "2026-09-01 10:00:00"), MakeReport(2, "garbled")};
        Result r = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow));
        CHECK(r.tooOld == 1 && r.sent == 1 && srv.got.size() == 1 && srv.got[0].body.find("DrawThing2") != std::string::npos);
    }
    {   // the request never carries anything but the nine columns, whatever the report said
        Server srv; Ledger ledger;
        Report r = MakeReport(1);
        r.text += "\nsecret=hunter2 ghp_abcdefghijklmnopqrstuvwxyz0123456789 10.1.2.3 Bearer abcdefghijklmnop\n";   // lint-allow: secret (a made-up value the scrubber must catch)
        Run({r}, ledger, srv.Transport_(), Alice(), opts(kNow));
        CHECK(srv.got.size() == 1 && srv.got[0].body.find("hunter2") == std::string::npos && srv.got[0].body.find("ghp_") == std::string::npos &&
              srv.got[0].body.find("10.1.2.3") == std::string::npos && srv.got[0].body.find("abcdefghijklmnop") == std::string::npos);
    }

    // ---- Only crashes from the moment sending was switched on are sent ------------------------------------------------------------------
    {
        std::vector<Report> reports = {MakeReport(2, "2026-11-15 11:45:00"), MakeReport(1, "2026-11-15 11:00:00"), MakeReport(3, "garbled"), MakeReport(4, "")};
        Server srv; Ledger ledger;
        ledger.SetSince("2026-11-15 11:30:00");
        Result r = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow));
        CHECK(r.sent == 1 && srv.got.size() == 1 && srv.got[0].body.find("DrawThing2") != std::string::npos && r.beforeSwitch == 3);   // the three older or undated ones stay
        Result again = Run(reports, ledger, srv.Transport_(), Alice(), opts(kNow + 60));
        CHECK(again.sent == 0 && srv.got.size() == 1);
        // Only older reports: nothing is sent and the page says why.
        Server srv2; Ledger l2; l2.SetSince("2026-11-15 11:30:00");
        Result o2 = Run({MakeReport(1, "2026-11-15 11:00:00")}, l2, srv2.Transport_(), Alice(), opts(kNow));
        CHECK(srv2.got.empty() && o2.beforeSwitch == 1 && o2.note == "Reports from before sending was switched on stay on this computer.");
        // The first run on a machine: no "since" yet, so Run sets it to now, and every report already there is held back.
        Server srv3; Ledger fresh;
        Options first = opts(kNow); first.nowLocal = "2026-11-15 12:00:00";
        Result f = Run({MakeReport(1, "2026-11-15 11:59:00"), MakeReport(2, "2026-10-01 08:00:00")}, fresh, srv3.Transport_(), Alice(), first);
        CHECK(srv3.got.empty() && f.beforeSwitch == 2 && f.changedLedger && fresh.Since() == "2026-11-15 12:00:00");
        // ... and a crash after that moment goes.
        Result next = Run({MakeReport(5, "2026-11-15 12:00:05"), MakeReport(1, "2026-11-15 11:59:00")}, fresh, srv3.Transport_(), Alice(), opts(kNow + 60));
        CHECK(next.sent == 1 && srv3.got.size() == 1 && srv3.got[0].body.find("DrawThing5") != std::string::npos);
        // Without a time from the caller nothing is held back (the unit tests above rely on it); a ledger without "since" stays without.
        Server srv4; Ledger plain;
        CHECK(Run({MakeReport(1, "2026-11-15 11:59:00")}, plain, srv4.Transport_(), Alice(), opts(kNow)).sent == 1 && plain.Since().empty());
        // On disk and hostile.
        Ledger rt = Ledger::Parse(fresh.Serialize());
        CHECK(rt.Since() == "2026-11-15 12:00:00");
        CHECK(Ledger::Parse("since garbage\nsince 2026-11-15\nsince 2026-13-99 25:61:61x\n").Since().empty());
        CHECK(Ledger::Parse("since 2026-11-15 12:00:00\n").Since() == "2026-11-15 12:00:00" && Ledger::Parse("since 2026-11-15 12:00:00\r\n").Since() == "2026-11-15 12:00:00");
        Ledger bad; bad.SetSince("../../etc"); CHECK(bad.Since().empty());
    }

    // ---- Report it yourself on GitHub: the prefilled address -------------------------------------------------------------------------------
    {
        Report r = MakeReport(7, "2026-11-15 10:00:00", 60);
        const IssueDraft d = MakeIssueDraft(r, Alice());
        CHECK(d.title == "Crash: Access violation in OkayEngine.exe!DrawThing7 (v2.874.0)");
        CHECK(d.body.find("<!-- okay-crash-signature: " + r.signature + " -->") == 0);
        CHECK(d.body.find("| Version | `2.874.0` |") != std::string::npos && d.body.find("| GPU | `Test GPU / Vendor / OpenGL 4.6` |") != std::string::npos);
        CHECK(d.body.find("### Backtrace\n```text\n#00 OkayEngine.exe + 0x1A2B3C  DrawThing7 +0x40") != std::string::npos);
        CHECK(d.body.find("### How to reproduce") != std::string::npos && d.body.find("alice") == std::string::npos && d.body.find("%USERPROFILE%") != std::string::npos);
        CHECK(d.body.find("Console (last 30 lines)") != std::string::npos && d.trimmed);   // 60 lines logged, 30 kept by default
        const std::string url = IssueUrl("owner/repo", r, Alice(), 7000);
        CHECK(url.size() <= 7000 && url.find("https://github.com/owner/repo/issues/new?labels=crash&title=Crash%3A%20Access%20violation") == 0);
        CHECK(url.find("&body=") != std::string::npos && url.find(' ') == std::string::npos && url.find('\n') == std::string::npos && url.find('<') == std::string::npos);
        bool trimmed = false;
        const std::string tiny = IssueUrl("owner/repo", r, Alice(), 1500, &trimmed);   // even a tight limit gives a valid address
        CHECK(tiny.size() <= 1500 && trimmed && tiny.find("issues/new?labels=crash&title=") != std::string::npos);
        const std::string roomy = IssueUrl("owner/repo", MakeReport(7, "2026-11-15 10:00:00", 3), Alice(), 20000, &trimmed);
        CHECK(roomy.find("Console") != std::string::npos && !trimmed);
        // Hostile: mentions, issue links, markdown and long text in the fields cannot become live in the title or leave a code block.
        Report h = MakeReport(8);
        const std::size_t endAt = h.text.find("--- End of report ---");
        h.text.insert(endAt, "[-1s] E frag ``` ```` ````` end ```\n[-1s] E " + std::string(3000, 'L') + "\n");
        Report p2 = ParseOne(h.text);
        p2.exception = "0xC0000005";
        const IssueDraft hd = MakeIssueDraft(p2, Alice(), 30);
        CHECK(hd.title.size() <= 110 && hd.title.find('@') == std::string::npos && hd.title.find('#') == std::string::npos && hd.title.find('\n') == std::string::npos);
        CHECK(hd.body.find("LLLLLLLLLL...[cut]") != std::string::npos && hd.body.find(std::string(450, 'L')) == std::string::npos);   // the long line is cut
        CHECK(hd.body.find("``````text\n") != std::string::npos);   // the Console fence is 6 backticks: longer than the 5-backtick run inside it
        Report m = ParseOne(MakeReport(9).text);
        m.version = "1.0 @everyone #123 `x`\n";
        const IssueDraft md = MakeIssueDraft(m, Alice(), 30);
        CHECK(md.title.find('@') == std::string::npos && md.title.find('#') == std::string::npos && md.title.find('\n') == std::string::npos);
        CHECK(PercentEncode("a b&c=d/é") == "a%20b%26c%3Dd%2F%C3%A9");
        CHECK(TitleText("@everyone #1\nline", 50) == "everyone 1 line" || TitleText("@everyone #1\nline", 50) == "everyone 1line");
        CHECK(CodeCell("a`b|c\n@x", 50) == "`a'b/c x`");
    }

    TEST_MAIN_RESULT();
}
