// The editor's sign-in gate (editor/SignInGate.hpp; owner, 2026-11-25: "i want users to be forced to sign in or they cant
// use anything", "fully blocked", "if its fully exported no"): no session is blocked, a session the server confirms opens,
// a session that cannot be confirmed (server unreachable, rejected) is blocked with no offline grace, the test switch is
// the only way round it, and games (the player, an exported game) are never gated.
#include "test_framework.hpp"
#include "SignInGate.hpp"
#include "okay/Core/Base64.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace acc = okay::account;
using namespace oked::signin;

static std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}


// ---- A fake Supabase auth server (AccountService::SetTransportForTest), for the launcher/editor session hand-off ----
// Access tokens are JWT-shaped (the payload carries "exp", as Supabase's do); refresh tokens work once (Supabase rotates them).
static std::string B64Url(const std::string& in) {   // the engine's one Base64 (okay/Core/Base64.hpp), URL alphabet, no padding
    return okay::base64::Encode(reinterpret_cast<const std::uint8_t*>(in.data()), in.size(), okay::base64::Alphabet::Url, false);
}
struct FakeAuthServer {
    std::set<std::string> access, refresh;   // what the server accepts right now
    int n = 0;
    std::vector<std::string> log;            // "who method path" per request
    int failNext = 0;                        // the next N requests get no answer (a dropped kept-alive connection)
    std::function<void(const std::string& who, const std::string& url)> before;   // runs before a request is answered
    std::string Issue(std::string& refreshOut) {
        ++n;
        const std::string tok = B64Url("{\"alg\":\"none\"}") + "." +
            B64Url("{\"sub\":\"u1\",\"exp\":" + std::to_string((long)std::time(nullptr) + 3600) + ",\"n\":" + std::to_string(n) + "}") + ".sig";
        refreshOut = "r" + std::to_string(n);
        access.insert(tok); refresh.insert(refreshOut);
        return "{\"access_token\":\"" + tok + "\",\"refresh_token\":\"" + refreshOut + "\",\"user\":{\"user_metadata\":{\"username\":\"tester\"}}}";
    }
    okay::http::Result Handle(const std::string& who, const std::string& method, const std::string& url,
                              const std::vector<std::string>& hdrs, const std::string& body) {
        if (before) before(who, url);
        log.push_back(who + " " + method + " " + url.substr(url.find("/auth/")));
        okay::http::Result r;
        if (failNext > 0) { --failNext; r.error = "winhttp error 12030"; r.maybeSent = true; return r; }
        r.reached = true;
        std::string bearer;
        for (const std::string& h : hdrs) if (h.rfind("Authorization: Bearer ", 0) == 0) bearer = h.substr(22);
        if (url.find("/auth/v1/user") != std::string::npos) { r.status = access.count(bearer) ? 200 : 401; r.body = "{\"id\":\"u1\"}"; return r; }
        if (url.find("grant_type=password") != std::string::npos) { std::string rt; r.status = 200; r.body = Issue(rt); return r; }
        if (url.find("grant_type=refresh_token") != std::string::npos) {
            const std::string rt = acc::detail::JsonField(body, "refresh_token");
            if (!refresh.erase(rt)) { r.status = 400; r.body = "{\"error_description\":\"Invalid Refresh Token: Already Used\"}"; return r; }
            std::string nr; r.status = 200; r.body = Issue(nr); return r;
        }
        r.status = 404; return r;
    }
    void Attach(acc::AccountService& a, const std::string& who) {
        a.SetTransportForTest([this, who](const std::string& m, const std::string& u, const std::vector<std::string>& h, const std::string& b) {
            return Handle(who, m, u, h, b);
        });
    }
};
static std::vector<std::string> SavedSessionLines(const fs::path& d) {
    std::vector<std::string> v; std::ifstream f(d / "session.txt"); std::string l;
    while (std::getline(f, l)) v.push_back(l);
    return v;
}

int main() {
    RUN_SUITE("editor_signin_gate");
    const fs::path dir = fs::temp_directory_path() / ("okay_signin_test_" + acc::detail::RandomHex(8));
    fs::remove_all(dir);

    {   // No saved session: blocked, the sign-in window shows.
        acc::AccountService a(dir / "none");
        CHECK(!a.IsLoggedIn());
        CHECK(a.CheckSession() == acc::SessionCheck::NoSession);
        const Phase p = Start(false, a.IsLoggedIn());
        CHECK(p == Phase::NeedSignIn);
        CHECK(!ProjectsAllowed(p));
        std::string msg;
        CHECK(AfterCheck(acc::SessionCheck::NoSession, msg) == Phase::NeedSignIn);
    }

    {   // A valid session opens: signed in, the check confirms it, projects may open.
        acc::AccountService a(dir / "valid");   // the on-device backend (no server set): its own check is the confirmation
        CHECK(a.Register("tester", "secret123").ok);
        acc::AccountService again(dir / "valid");   // a new start reads the saved session, as the editor reads the launcher's
        CHECK(again.IsLoggedIn());
        Phase p = Start(false, again.IsLoggedIn());
        CHECK(p == Phase::Checking);
        CHECK(!ProjectsAllowed(p));   // nothing opens while it is being checked
        std::string msg = "x";
        p = AfterCheck(again.CheckSession(), msg);
        CHECK(p == Phase::Open);
        CHECK(ProjectsAllowed(p));
        CHECK(msg.empty());
    }

    {   // A saved session the server cannot confirm is blocked: no offline grace (owner: "fully blocked").
        fs::create_directories(dir / "offline");
        { std::ofstream f(dir / "offline" / "session.txt"); f << "tester\nsome.saved.token\nrefresh\n"; }
        acc::AccountService a(dir / "offline", "http://127.0.0.1:1", "anon-key");   // nothing listens on port 1
        CHECK(a.IsLoggedIn());
        const acc::SessionCheck c = a.CheckSession();
        CHECK(c == acc::SessionCheck::Unreachable);
        std::string msg;
        const Phase p = AfterCheck(c, msg);
        CHECK(p == Phase::Unreachable);
        CHECK(!ProjectsAllowed(p));
        CHECK(msg.find("could not be confirmed") != std::string::npos);   // under the heading "Can't reach the OkaySpace server"
        CHECK(a.IsLoggedIn());   // the session is kept: Try again works once the server answers
        CHECK(a.VerifySession());   // and the launcher's own check still keeps players signed in offline (unchanged)
    }

    {   // Rejected (expired and not renewed) and server errors are blocked too.
        std::string msg;
        CHECK(AfterCheck(acc::SessionCheck::Rejected, msg) == Phase::NeedSignIn && msg.find("expired") != std::string::npos);
        CHECK(AfterCheck(acc::SessionCheck::ServerError, msg) == Phase::Unreachable);
        acc::Result bad; bad.ok = false; bad.error = "Wrong password.";
        CHECK(AfterSignIn(bad, msg) == Phase::NeedSignIn && msg == "Wrong password.");
        acc::Result good; good.ok = true;
        CHECK(AfterSignIn(good, msg) == Phase::Open && msg.empty());
        CHECK(!ProjectsAllowed(Phase::SigningIn));
    }

    {   // The test and CI switch is the only way round it, and only the exact value "1".
        CHECK(BypassRequested("1"));
        CHECK(!BypassRequested(nullptr) && !BypassRequested("") && !BypassRequested("0") && !BypassRequested("yes") && !BypassRequested("11"));
        CHECK(Start(true, false) == Phase::Off && ProjectsAllowed(Phase::Off));
    }

    {   // Games are never gated: only the editor asks for an OkaySpace account, and the player (which is what an exported
        // game ships) has no sign-in gate in it.
        CHECK(GateAppliesTo(App::Editor));
        CHECK(!GateAppliesTo(App::Player) && !GateAppliesTo(App::ExportedGame) && !GateAppliesTo(App::DedicatedServer) && !GateAppliesTo(App::Launcher));
        const std::string player = ReadText(fs::path(OKAY_SOURCE_DIR) / "player" / "main.cpp");
        CHECK(!player.empty());
        CHECK(player.find("SignInGate") == std::string::npos);
        CHECK(player.find("CheckSession") == std::string::npos);
        CHECK(player.find("OKAY_TEST_NO_SIGNIN") == std::string::npos);   // and it needs no switch to run
    }

    // ---- The launcher and the editor share session.txt (owner, 2026-11-25: "signing in works, but sometimes the editor
    // doesnt recognize it") ----
    const std::string url = "https://fake.supabase.test", key = "anon-key-not-secret";
    {   // The editor was waiting at the sign-in window; the player signs in in the LAUNCHER. The editor sees the file change,
        // reads it and opens, with no restart.
        FakeAuthServer srv;
        const fs::path d = dir / "handoff";
        acc::AccountService editor(d, url, key); srv.Attach(editor, "editor");
        Phase p = Start(false, editor.IsLoggedIn());
        CHECK(p == Phase::NeedSignIn);
        const long long stamp0 = editor.SessionFileStamp();
        CHECK(stamp0 == -1);   // no file yet
        acc::AccountService launcher(d, url, key); srv.Attach(launcher, "launcher");
        CHECK(launcher.Login("tester@example.com", "secret123").ok);
        CHECK(editor.SessionFileStamp() != stamp0);
        CHECK(RecheckWhenSessionFileChanges(p, /*jobRunning=*/false));
        CHECK(!RecheckWhenSessionFileChanges(p, /*jobRunning=*/true));
        CHECK(!RecheckWhenSessionFileChanges(Phase::Open, false) && !RecheckWhenSessionFileChanges(Phase::SigningIn, false));
        CHECK(RecheckWhenSessionFileChanges(Phase::Unreachable, false));
        CHECK(editor.SyncSessionFromDisk());
        CHECK(editor.IsLoggedIn());
        std::string msg;
        p = AfterCheck(editor.CheckSession(), msg);
        CHECK(p == Phase::Open);
        // Signed out in the launcher: the editor's next read follows (it does not keep a sign-in the player ended).
        launcher.Logout();
        CHECK(editor.SyncSessionFromDisk() && !editor.IsLoggedIn());
        CHECK(!editor.SyncSessionFromDisk());   // nothing new
    }
    {   // Signed out and in again in the launcher while the editor still holds the OLD sign-in in memory: the editor's check
        // reads the file first and confirms the new one.
        FakeAuthServer srv;
        const fs::path d = dir / "relogin";
        acc::AccountService launcher(d, url, key); srv.Attach(launcher, "launcher");
        CHECK(launcher.Login("tester@example.com", "secret123").ok);
        acc::AccountService editor(d, url, key); srv.Attach(editor, "editor");
        CHECK(editor.IsLoggedIn());
        launcher.Logout();
        srv.access.clear(); srv.refresh.clear();   // signing out ends that sign-in on the server too
        CHECK(launcher.Login("tester@example.com", "secret123").ok);
        CHECK(editor.CheckSession() == acc::SessionCheck::Valid);
        CHECK(editor.CurrentSession().token == launcher.CurrentSession().token);
        CHECK(SavedSessionLines(d).size() >= 2);   // the launcher's sign-in is still saved
    }
    {   // The refresh race: the editor's access token expired and, at the same moment, the launcher refreshes with the same
        // single-use refresh token and saves the new pair. The editor's own refresh is refused ("Already Used"). Before the
        // fix the editor then signed out and DELETED session.txt, which held the launcher's new sign-in. Now it takes the
        // launcher's new token, and the file stays.
        FakeAuthServer srv;
        const fs::path d = dir / "race";
        acc::AccountService launcher(d, url, key); srv.Attach(launcher, "launcher");
        CHECK(launcher.Login("tester@example.com", "secret123").ok);
        acc::AccountService editor(d, url, key); srv.Attach(editor, "editor");
        srv.access.clear();   // the access token expired
        bool raced = false;
        srv.before = [&](const std::string& who, const std::string& u) {
            if (who == "editor" && u.find("grant_type=refresh_token") != std::string::npos && !raced) {
                raced = true;   // the launcher's refresher wins by a hair
                CHECK(launcher.CheckSession() == acc::SessionCheck::Valid);
            }
        };
        const acc::SessionCheck c = editor.CheckSession();
        CHECK(raced);
        CHECK(c == acc::SessionCheck::Valid);
        CHECK(editor.IsLoggedIn());
        CHECK(editor.CurrentSession().token == launcher.CurrentSession().token);
        const std::vector<std::string> lines = SavedSessionLines(d);
        CHECK(lines.size() >= 3 && lines[1] == launcher.CurrentSession().token);   // the launcher's sign-in was not deleted
        acc::AccountService later(d, url, key);
        CHECK(later.IsLoggedIn());   // a new start (the launcher's pollers read the file) is still signed in
    }
    {   // A sign-in that really was rejected (expired, and the refresh token is gone) still signs out and clears the file.
        FakeAuthServer srv;
        const fs::path d = dir / "rejected";
        acc::AccountService a(d, url, key); srv.Attach(a, "editor");
        CHECK(a.Login("tester@example.com", "secret123").ok);
        srv.access.clear(); srv.refresh.clear();
        std::string msg;
        CHECK(AfterCheck(a.CheckSession(), msg) == Phase::NeedSignIn);
        CHECK(!a.IsLoggedIn());
        CHECK(!fs::exists(d / "session.txt"));
    }
    {   // The editor's "Couldn't reach the account server" now says why, in plain words, and never shows the key or password.
        FakeAuthServer srv;
        const fs::path d = dir / "unreach";
        acc::AccountService a(d, url, key); srv.Attach(a, "editor");
        srv.failNext = 100;
        const acc::Result r = a.Login("tester@example.com", "secret123");
        CHECK(!r.ok);
        CHECK(r.error.rfind("Couldn't reach the account server (offline or unreachable).", 0) == 0);   // the first line is unchanged
        CHECK(r.error.find("connection was cut off") != std::string::npos);
        CHECK(r.error.find("12030") != std::string::npos);
        CHECK(r.error.find(key) == std::string::npos && r.error.find("secret123") == std::string::npos);
        CHECK(srv.log.size() == 2);   // the sign-in was tried twice (signing in again is harmless), not more
        std::string msg;
        CHECK(AfterSignIn(r, msg) == Phase::NeedSignIn && msg == r.error);
    }
    {   // A kept-alive connection the server closed while the window sat idle: the first try gets no answer, the second
        // signs in.
        FakeAuthServer srv;
        const fs::path d = dir / "dropped";
        acc::AccountService a(d, url, key); srv.Attach(a, "editor");
        srv.failNext = 1;
        const acc::Result r = a.Login("tester@example.com", "secret123");
        CHECK(r.ok);
        CHECK(a.IsLoggedIn());
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
