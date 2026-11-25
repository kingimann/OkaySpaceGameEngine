// The editor's sign-in gate (editor/SignInGate.hpp; owner, 2026-11-25: "i want users to be forced to sign in or they cant
// use anything", "fully blocked", "if its fully exported no"): no session is blocked, a session the server confirms opens,
// a session that cannot be confirmed (server unreachable, rejected) is blocked with no offline grace, the test switch is
// the only way round it, and games (the player, an exported game) are never gated.
#include "test_framework.hpp"
#include "SignInGate.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
namespace acc = okay::account;
using namespace oked::signin;

static std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

int main() {
    RUN_SUITE("signin_gate");
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

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
