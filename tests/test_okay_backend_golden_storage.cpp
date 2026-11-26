// OkayBackend round 4: golden request tests for the Storage and Functions parts (docs/platform/okay_backend.md, sections 6,
// 10.1 and 17).
//
// Every Storage and Functions request the programs send is run against a fake server through the programs' own code: chat
// attachments and the personal media library (Messages, bucket "chat"), workshop uploads (Workshop, bucket "workshop"), the
// launcher's Official game admin tab (launcher/OfficialAdmin.hpp, bucket "releases", folder official/), "Delete my account"
// (AccountData, function delete-account, sent as the launcher sends it) and the session ticket (TicketClient through
// TicketJoin's account transport and through the launcher's pre-flight form, function session-ticket). The exact requests
// (method, URL, header lines in order, body, and for an upload the bytes of the file sent as the body, and any time limit) and
// what the program made of each answer must equal the GOLDEN texts in test_okay_backend_golden_storage.inc, recorded from the
// code BEFORE the move (run with OKAY_GOLDEN_PRINT=1 to print them again). Then the same requests are sent through the
// Supabase provider directly (okay::backend::supabase::Storage / Functions over a RecordingTransport) and must be the same.
//
// The updater: ReleasesBucketBase (launcher/VersionsManager.cpp, the one builder of the address every installed launcher and
// editor updates from) must give exactly the texts recorded from the old code for a set of inputs, and must never follow the
// backend configuration (OKAY_BACKEND, backend.json).
//
// Two parts of some addresses change from run to run: the random part of an uploaded object's name (16 hex digits) and the
// official tab's cache-buster ("?cb=<time>-<ticks>"). The fake server writes them as <RND> and <CB>.
//
// No request ever leaves the process. The address, key, names and tokens are made up.
#include "test_framework.hpp"
#include "okay_backend_golden.hpp"   // the fake server, made-up tokens, RequestsOf
#include "okay/Backend/Backend.hpp"
#include "okay/Platform/Account/Account.hpp"
#include "okay/Platform/Account/AccountData.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Messages.hpp"
#include "okay/Platform/Workshop.hpp"
#include "OfficialAdmin.hpp"          // launcher/: the Official game admin tab's requests
#include "TicketJoin.hpp"             // shared/: the session ticket through the shared account
#include "VersionsManager.hpp"        // launcher/: ReleasesBucketBase

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <regex>
#include <string>
#include <vector>

// The engine's clock, for the official tab's cache-buster (the test does not link the engine; the fake server writes the
// cache-buster as <CB>, so its value does not matter).
namespace okay::platform { uint64_t TicksMs() { return 12345; } }

namespace fs = std::filesystem;
namespace acc = okay::account;
namespace ob = okay::backend;
namespace tk = okayac::ticket;
using golden::kUrl; using golden::kKey; using golden::Tok; using golden::Fake; using golden::Http; using golden::NoAnswer;
using golden::SaveSession;

// ---- Helpers -------------------------------------------------------------------------------------------------------------
/// The random object names and the cache-buster as fixed marks.
static std::string Steady(const std::string& s) {
    static const std::regex rnd("/[0-9a-f]{16}-"), cb("\\?cb=[0-9]+-[0-9]+");
    return std::regex_replace(std::regex_replace(s, rnd, "/<RND>-"), cb, "?cb=<CB>");
}
/// Bytes as text: printable ASCII as it is, everything else as \xHH.
static std::string Printable(const std::string& b) {
    static const char* hx = "0123456789abcdef";
    std::string o;
    for (unsigned char c : b) {
        if (c >= 0x20 && c < 0x7f && c != '\\') o += (char)c;
        else { o += "\\x"; o += hx[c >> 4]; o += hx[c & 15]; }
    }
    return o;
}
static std::string ReadFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "<no file>";
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
/// The parts of a request the HTTP hook does not see, written into the request's text: the time limit as one more line after
/// the header lines, and an upload's body as the bytes of its file.
static std::vector<std::string> HeadersOf(const ob::Request& r) {
    std::vector<std::string> h = r.headers;
    if (r.timeoutSec > 0) h.push_back("(time limit: " + std::to_string(r.timeoutSec) + " s)");
    return h;
}
static std::string BodyOf(const ob::Request& r) {
    return r.bodyFile.empty() ? r.body : "(file bytes) " + Printable(ReadFile(r.bodyFile));
}

static void Hook(acc::AccountService& a, Fake& f) {
    auto seen = std::make_shared<ob::Request>();
    a.ObserveRequestsForTest([seen](const ob::Request& r) { *seen = r; });
    a.SetTransportForTest([&f, seen](const std::string& m, const std::string& u, const std::vector<std::string>&, const std::string&) {
        return f.Answer(m, u, HeadersOf(*seen), BodyOf(*seen));
    });
    f.rewriteUrl = [](const std::string& u) { return Steady(u); };
}
static std::unique_ptr<acc::AccountService> Make(const fs::path& dir, Fake& f, int tok = 1) {
    SaveSession(dir, "alex", Tok(tok), "r" + std::to_string(tok));
    auto a = std::make_unique<acc::AccountService>(dir, kUrl, kKey);
    Hook(*a, f);
    return a;
}
static std::string B(bool v) { return v ? "1" : "0"; }
static void WriteBytes(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << bytes;
}
static const std::string kBinary = std::string("PNG\x89\r\n\x1a\n", 8) + std::string("\0\x01\xff body", 8);
static const char* kMe = "[{\"id\":\"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7\"}]";
static const std::string kBind(43, 'k');            // a connection binding (NetworkManager::TicketBinding shape)
static const std::string kHw(64, 'a');              // a machine hash
static std::string TicketAnswer() {                 // the time is in the answer only, never in a request or the result text
    const long now = (long)std::time(nullptr);
    return "{\"ticket\":\"okt1.payload.sig\",\"exp\":" + std::to_string(now + 600) + ",\"iat\":" + std::to_string(now) + "}";
}
static std::string Ticket(const tk::Result& r) {
    return "=> outcome=" + std::to_string((int)r.outcome) + " ticket=[" + r.ticket + "] attempts=" + std::to_string(r.attempts) +
           " message=[" + r.message + "]\n";
}
static tk::Options FastOptions() {
    tk::Options o;
    o.sleep = [](double) {};   // retries without waiting
    return o;
}
/// The launcher's pre-flight transport (launcher/AntiCheatPages.cpp, TicketPreflightWorker), as it sends.
static tk::Transport LauncherTicketTransport(acc::AccountService& a) {
    return [&a](const okayac::backend::Request& r) {
        const okay::account::ApiResponse x = a.ApiTimed(r.path, r.method, r.body, r.header, 8);
        tk::Reply out; out.reached = x.reached; out.status = x.status; out.body = x.body; return out;
    };
}
/// "Delete my account" as the launcher sends it (launcher/main.cpp, AcctDeleteWorker).
static std::string DeleteAccount(acc::AccountService& a, const std::string& user, const std::string& pw) {
    namespace ad = okay::account::data;
    ad::DeleteRequest q = ad::BuildDeleteRequest(user, pw);
    okay::account::ApiResponse r = a.Api(q.path, q.method, q.body);
    ad::DeleteOutcome o = ad::MapDeleteResponse(r);
    return "=> ok=" + B(o.ok) + " signOut=" + B(o.signOut) + " notDeployed=" + B(o.notDeployed) + " step=[" + o.failedStep +
           "] message=[" + o.message + "]\n";
}
static std::string Items(const std::vector<okay::Messages::LibraryItem>& v) {
    std::string s;
    for (const auto& it : v) s += "  item name=[" + it.name + "] file=[" + it.file + "] url=[" + it.url + "]\n";
    return s;
}

// ---- The scenarios, run through the programs' own code ---------------------------------------------------------------------
using golden::Scenario;

static std::vector<Scenario> Scenarios() {
    std::vector<Scenario> v;

    // Messages: chat attachments (bucket "chat").
    v.push_back({"chat_attachment", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, kMe), Http(200, "{\"Key\":\"chat/x\"}")};
        auto a = Make(d, f);
        WriteBytes(d / "files" / "level plan (v2).png", kBinary);
        okay::Messages m(*a);
        std::string err;
        const std::string url = m.UploadChatFile((d / "files" / "level plan (v2).png").string(), &err);
        return f.log + "=> url=[" + Steady(url) + "] err=[" + err + "]\n";
    }});
    v.push_back({"chat_attachment_refused", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, kMe), Http(404, "{\"error\":\"Bucket not found\"}"), Http(413, "{\"error\":\"Payload too large\"}"),
                           NoAnswer("connect: winhttp error 12029")};
        auto a = Make(d, f);
        WriteBytes(d / "a.bin", "x");
        okay::Messages m(*a);
        std::string out;
        for (int i = 0; i < 3; ++i) {
            std::string err;
            const std::string url = m.UploadChatFile((d / "a.bin").string(), &err);
            out += "=> url=[" + Steady(url) + "] err=[" + err + "]\n";
        }
        return f.log + out;
    }});
    v.push_back({"chat_attachment_signed_out", [](const fs::path& d) {
        Fake f;
        auto a = std::make_unique<acc::AccountService>(d, kUrl, kKey);
        Hook(*a, f);
        okay::Messages m(*a);
        std::string err;
        const std::string url = m.UploadChatFile((d / "a.bin").string(), &err);
        return f.log + "=> url=[" + url + "] err=[" + err + "]\n";
    }});
    // Messages: the personal media library (same bucket, <me>/emoji/ and <me>/stickers/).
    v.push_back({"library", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, kMe), Http(200, "{\"Key\":\"chat/x\"}"), Http(500, "boom"),
                           Http(200, "[{\"name\":\"party.gif\",\"id\":\"1\"},{\"name\":\".emptyFolderPlaceholder\"},{\"name\":\"wave.png\",\"id\":\"2\"}]"),
                           Http(400, "{\"error\":\"bad\"}"), Http(200, "[]"), Http(404, "{}")};
        auto a = Make(d, f);
        WriteBytes(d / "Party.GIF", kBinary);
        okay::Messages m(*a);
        std::string out;
        const char* const uploads[][2] = {{"emoji", "Party Parrot!"}, {"stickers", "wave"}, {"emoji", "!!!"}};   // the last: no request
        for (const auto& u : uploads) {
            std::string err;
            const std::string url = m.UploadToLibrary((d / "Party.GIF").string(), u[0], u[1], &err);
            out += "=> url=[" + url + "] err=[" + err + "]\n";
        }
        out += "=> list\n" + Items(m.ListLibrary("emoji"));
        out += "=> list\n" + Items(m.ListLibrary("stickers"));
        out += "=> delete " + B(m.DeleteFromLibrary("emoji", "party.gif")) + "\n";
        out += "=> delete " + B(m.DeleteFromLibrary("emoji", "gone.png")) + "\n";
        out += "=> delete " + B(m.DeleteFromLibrary("emoji", "")) + "\n";   // no request
        out += "=> public [" + m.PublicChatUrl("u-1/emoji/x.png") + "]\n";
        return f.log + out;
    }});

    // Workshop uploads (bucket "workshop").
    v.push_back({"workshop_upload", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, kMe), Http(200, "{\"Key\":\"workshop/x\"}"),
                           Http(200, kMe), Http(413, "{\"error\":\"too large\"}"),
                           Http(200, kMe), Http(404, "{\"message\":\"Bucket not found\"}"),
                           Http(200, "[]")};
        auto a = Make(d, f);
        WriteBytes(d / "My Map #1.zip", kBinary);
        okay::Workshop w(*a);
        std::string out;
        for (int i = 0; i < 4; ++i) {
            std::string err;
            const std::string url = w.UploadFile((d / "My Map #1.zip").string(), &err);
            out += "=> url=[" + Steady(url) + "] err=[" + err + "]\n";
        }
        return f.log + out;
    }});

    // The launcher's Official game admin tab (bucket "releases", folder official/).
    v.push_back({"official_reads", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "1.2.0\nmore"), Http(404, "{}"), NoAnswer("timeout"),
                           Http(200, "{\"name\":\"x\"}"), Http(200, "notes")};
        auto a = Make(d, f);
        std::string out;
        for (int i = 0; i < 3; ++i) {
            bool reached = false;
            const std::string live = OaLive(*a, &reached);
            out += "=> live [" + live + "] reached=" + B(reached) + "\n";
        }
        og::Manifest m;
        const bool manifest = OaManifest(*a, "1.2.0", m);
        out += "=> manifest " + B(manifest) + "\n";
        const auto n = OaGet(*a, "a b/ü&?.txt");
        out += "=> get ok=" + B(n.ok) + " body=[" + n.body + "]\n";
        return f.log + out;
    }});
    v.push_back({"official_uploads", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "{}"), Http(403, "{\"error\":\"new row violates row-level security\"}"), Http(200, "{}"),
                           NoAnswer("connect")};
        auto a = Make(d, f);
        WriteBytes(d / "game" / "data 1.pak", kBinary);
        std::string out;
        const auto r = OaUpload(*a, "v1.2.0/data 1.pak", d / "game" / "data 1.pak", "application/octet-stream", 31536000);
        out += "=> upload ok=" + B(r.ok) + "\n";
        bool needSql = false;
        const auto r2 = OaUpload(*a, "v1.2.0/data 1.pak", d / "game" / "data 1.pak", "application/octet-stream", 31536000);
        const std::string error = OaError(r2, "Uploading data 1.pak", needSql);
        out += "=> upload ok=" + B(r2.ok) + " error=[" + error + "] needSql=" + B(needSql) + "\n";
        okay::account::ApiResponse t;
        const bool notes = OaUploadText(*a, "NOTES.txt", "line one\nline \"two\"\n", "text/plain; charset=utf-8", 30, &t);
        out += "=> text " + B(notes) + "\n";
        const bool version = OaUploadText(*a, "VERSION.txt", "1.2.0\n", "text/plain", 30, &t);
        out += "=> text " + B(version) + " reached=" + B(t.reached) + "\n";
        return f.log + out;
    }});
    v.push_back({"official_lists", [](const fs::path& d) {
        // A full page (1000) asks for the next one; a folder is listed inside; then the top for the tab's refresh.
        std::string page = "[";
        for (int i = 0; i < 1000; ++i) page += std::string(i ? "," : "") + "{\"name\":\"f" + std::to_string(i) + ".bin\",\"id\":\"" + std::to_string(i) + "\"}";
        page += "]";
        Fake f; f.queue = {Http(200, page), Http(200, "[{\"name\":\"sub dir\",\"id\":null},{\"name\":\"last.bin\",\"id\":\"9\"}]"),
                           Http(200, "[{\"name\":\"deep \\\"q\\\".bin\",\"id\":\"8\"}]"),
                           Http(200, "[{\"name\":\"v1.2.0\",\"id\":null},{\"name\":\"VERSION.txt\",\"id\":\"1\"}]"),
                           Http(500, "{}")};
        auto a = Make(d, f);
        std::vector<std::string> names;
        const bool all = OaListAll(*a, "official/v1.2.0/", names);
        std::string out = "=> all " + B(all) + " count=" + std::to_string(names.size());
        if (!names.empty()) out += " first=[" + names.front() + "] last=[" + names.back() + "]";
        out += "\n";
        const auto top = OaListTop(*a);
        out += "=> top ok=" + B(top.ok) + " body=[" + top.body + "]\n";
        std::vector<std::string> none;
        const bool any = OaListAll(*a, "official/v9/", none);
        out += "=> all " + B(any) + " count=" + std::to_string(none.size()) + "\n";
        return f.log + out;
    }});
    v.push_back({"official_delete", [](const fs::path& d) {
        std::string many = "[";   // 201 files: two delete requests of 200 and 1
        for (int i = 0; i < 201; ++i) many += std::string(i ? "," : "") + "{\"name\":\"f" + std::to_string(i) + "\",\"id\":\"" + std::to_string(i) + "\"}";
        many += "]";
        Fake f; f.queue = {Http(200, "1.3.0\n"), Http(200, many), Http(200, "[{\"name\":\"f0\"},{\"name\":\"f1\"}]"),
                           Http(200, "[{\"name\":\"f200\"}]"), Http(204, ""),
                           Http(200, "1.3.0\n"),
                           Http(200, "1.3.0\n"), Http(200, "[{\"name\":\"x\\\"y.bin\",\"id\":\"1\"}]"), Http(400, "{\"statusCode\":\"403\",\"error\":\"Unauthorized\"}")};
        auto a = Make(d, f);
        std::string out, why;
        bool needSql = false;
        for (const char* ver : {"1.2.0", "1.3.0", "1.1.0"}) {
            why.clear(); needSql = false;
            const bool done = OaDeleteVersion(*a, ver, why, needSql);
            out += "=> delete " + B(done) + " why=[" + why + "] needSql=" + B(needSql) + "\n";
        }
        return f.log + out;
    }});

    // Functions: "Delete my account" (delete-account), as the launcher sends it.
    v.push_back({"delete_account", [](const fs::path& d) {
        Fake f; f.queue = {Http(404, "{\"code\":\"NOT_FOUND\"}"), Http(500, "{\"error\":\"x\",\"failed_step\":\"forum_posts\"}"),
                           NoAnswer("timeout"), Http(200, "{\"ok\":true,\"steps\":[]}")};
        auto a = Make(d, f);
        std::string out;
        out += DeleteAccount(*a, "al\"ex", "pa\\ss\x01word");
        out += DeleteAccount(*a, "alex", "pw");
        out += DeleteAccount(*a, "alex", "pw");
        out += DeleteAccount(*a, "alex", "pw");
        return f.log + out;
    }});
    v.push_back({"delete_account_signed_out", [](const fs::path& d) {
        Fake f;
        auto a = std::make_unique<acc::AccountService>(d, kUrl, kKey);
        Hook(*a, f);
        return f.log + DeleteAccount(*a, "alex", "pw");
    }});

    // Functions: the session ticket through the shared account (TicketJoin's transport, what a game's join gate uses).
    v.push_back({"session_ticket_shared", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, TicketAnswer()), Http(500, "{\"error\":\"boom\"}"), Http(200, TicketAnswer()),
                           Http(404, "{\"error\":\"no function\"}"), Http(403, "{\"error\":\"banned\",\"until\":0}")};
        SaveSession(d, "alex", Tok(3), "r3");
        okay::Account::Configure(d.string(), kUrl, kKey);
        Hook(okay::Account::Get(), f);
        tk::Client c(tk::AccountTransport(), FastOptions());
        std::string out = Ticket(c.Acquire("arena-1", kHw, kBind));
        out += Ticket(c.Acquire("203.0.113.7:45000", "", ""));
        out += Ticket(c.Acquire("arena-2", "", kBind));
        out += Ticket(c.Acquire("arena-3", "", ""));
        out += Ticket(c.Acquire("bad id", "", ""));   // refused before anything is sent
        okay::Account::Get().SetTransportForTest({});
        okay::Account::Get().ObserveRequestsForTest({});
        return f.log + out;
    }});
    // ...and the launcher's pre-flight form (its own account service, the same 8 second limit).
    v.push_back({"session_ticket_launcher", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, TicketAnswer()), NoAnswer("timeout")};
        auto a = Make(d, f, 4);
        tk::Options o = FastOptions();
        o.maxAttempts = 1;
        tk::Client c(LauncherTicketTransport(*a), o);
        std::string out = Ticket(c.Acquire("arena-1", "", kBind));
        out += Ticket(c.Acquire("arena-1", kHw, kBind));
        return f.log + out;
    }});

    // The updater's address (no request: the text every installed launcher and editor updates from).
    v.push_back({"releases_bucket_base", [](const fs::path&) {
        const char* const cases[][2] = {
            {"", "https://abc123.supabase.co"}, {"", "https://abc123.supabase.co/"}, {"", "https://abc123.supabase.co///"},
            {"https://other.supabase.co", "https://abc123.supabase.co"}, {"http://127.0.0.1:9000", ""}, {"http://127.0.0.1:9000/", "x"},
            {"", ""}, {"", "http://abc.supabase.co"}, {"", "junk"}, {"", "https://github.com/o/r"}, {"https://api.github.com", ""},
            {"", "https://raw.githubusercontent.com/o/r"}, {"", "https://abc.supabase.co/sub/path"}, {"", "https://abc.supabase.co?x=1"},
            {"", "https://abc.supabase.co/ space"}, {"", "https://"}, {"http://localhost:9000", ""}, {"", "HTTPS://ABC.SUPABASE.CO"},
        };
        std::string out;
        for (const auto& c : cases) out += "[" + std::string(c[0]) + "] [" + c[1] + "] => [" + okayversions::ReleasesBucketBase(c[0], c[1]) + "]\n";
        return out;
    }});
    return v;
}

#include "test_okay_backend_golden_storage.inc"

int main() {
    RUN_SUITE("okay_backend_golden_storage");
    const bool print = std::getenv("OKAY_GOLDEN_PRINT") != nullptr;
#if defined(_WIN32)
    _putenv_s("OKAY_BACKEND", "");
#else
    unsetenv("OKAY_BACKEND");   // the developer's own setting must not decide these results
#endif
    const fs::path root = fs::temp_directory_path() / ("okay_backend_golden_storage_" + acc::detail::RandomHex(8));
    golden::RunScenarios(__FILE__, Scenarios(), kGolden, root, print);
    if (print) { std::error_code ec; fs::remove_all(root, ec); return 0; }

    std::error_code ec;
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
