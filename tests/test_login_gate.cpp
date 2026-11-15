#include "test_framework.hpp"
#include "LoginGate.hpp"

#include <cstdio>
#include <filesystem>

using namespace okay::logingate;

// The launcher's sign-in gate decision (shared/LoginGate.hpp): signed in / valid / grace period / rejected, the on-off switch and the
// last-check file. Pure logic, no window and no network.
static Inputs base() {
    Inputs i; i.required = true; i.signedIn = true; i.onlineBackend = true;
    i.now = 2000000000L; i.tokenExpiresAt = i.now + 3000; i.lastOnlineCheck = i.now - 10;
    return i;
}

int main() {
    RUN_SUITE("login_gate");
    const long D = 24L * 60 * 60;

    // ---- The grace period is 7 days (a wrong constant fails here) ----
    CHECK(kOfflineGraceSeconds == 7 * D);

    // ---- Signed in with a valid token ----
    CHECK(Decide(base()) == Verdict::Allowed);
    { Inputs i = base(); i.lastOnlineCheck = 0; CHECK(Decide(i) == Verdict::Allowed); }   // a valid token needs no stored time

    // ---- Not signed in / rejected ----
    { Inputs i = base(); i.signedIn = false; CHECK(Decide(i) == Verdict::NeedSignIn); }
    { Inputs i = base(); i.signedIn = false; i.tokenExpiresAt = 0; i.lastOnlineCheck = i.now; CHECK(Decide(i) == Verdict::NeedSignIn); }   // refused = signed out: no grace

    // ---- Gate off ----
    { Inputs i = base(); i.required = false; i.signedIn = false; CHECK(Decide(i) == Verdict::Allowed); }

    // ---- Local backend has no server: signed in is enough ----
    { Inputs i = base(); i.onlineBackend = false; i.tokenExpiresAt = 0; i.lastOnlineCheck = 0; CHECK(Decide(i) == Verdict::Allowed); }
    { Inputs i = base(); i.onlineBackend = false; i.signedIn = false; CHECK(Decide(i) == Verdict::NeedSignIn); }

    // ---- Expired token: the grace period ----
    {
        Inputs i = base(); i.tokenExpiresAt = i.now - 60;
        i.lastOnlineCheck = i.now - 1 * D;           CHECK(Decide(i) == Verdict::Allowed);
        i.lastOnlineCheck = i.now - 6 * D;           CHECK(Decide(i) == Verdict::Allowed);
        i.lastOnlineCheck = i.now - 7 * D;           CHECK(Decide(i) == Verdict::Allowed);        // exactly 7 days still counts
        i.lastOnlineCheck = i.now - 7 * D - 1;       CHECK(Decide(i) == Verdict::GraceExpired);   // one second past
        i.lastOnlineCheck = i.now - 30 * D;          CHECK(Decide(i) == Verdict::GraceExpired);
    }
    // Unknown expiry behaves like expired.
    { Inputs i = base(); i.tokenExpiresAt = 0; i.lastOnlineCheck = i.now - D; CHECK(Decide(i) == Verdict::Allowed);
      i.lastOnlineCheck = i.now - 8 * D; CHECK(Decide(i) == Verdict::GraceExpired); }
    // Token expiring exactly now is expired.
    { Inputs i = base(); i.tokenExpiresAt = i.now; i.lastOnlineCheck = 0; CHECK(Decide(i) == Verdict::GraceExpired); }

    // ---- Missing last-check time: no grace ----
    { Inputs i = base(); i.tokenExpiresAt = i.now - 60; i.lastOnlineCheck = 0; CHECK(Decide(i) == Verdict::GraceExpired); }
    { Inputs i = base(); i.tokenExpiresAt = i.now - 60; i.lastOnlineCheck = -5; CHECK(Decide(i) == Verdict::GraceExpired); }

    // ---- Clock going backwards / last check in the future ----
    {
        Inputs i = base(); i.tokenExpiresAt = i.now - 60;
        i.lastOnlineCheck = i.now + 60;              CHECK(Decide(i) == Verdict::Allowed);        // small skew: treated as just now
        i.lastOnlineCheck = i.now + kClockSkewSeconds;      CHECK(Decide(i) == Verdict::Allowed);
        i.lastOnlineCheck = i.now + kClockSkewSeconds + 1;  CHECK(Decide(i) == Verdict::GraceExpired);   // clock set back / edited file
        i.lastOnlineCheck = i.now + 400 * D;         CHECK(Decide(i) == Verdict::GraceExpired);
    }
    // The clock jumping back must not stretch the grace: last check 6 days ago, then clock set back 3 days -> still allowed;
    // set back 10 days (last check now looks 4 days in the future) -> not trusted.
    {
        Inputs i = base(); i.tokenExpiresAt = i.now - 60; i.lastOnlineCheck = i.now - 6 * D;
        i.now -= 3 * D; i.tokenExpiresAt = i.now - 60; CHECK(Decide(i) == Verdict::Allowed);
        i.now -= 7 * D; i.tokenExpiresAt = i.now - 60; CHECK(Decide(i) == Verdict::GraceExpired);
    }

    // ---- An outage keeps the session (grace); a refusal has already signed out (signedIn=false) ----
    {
        Inputs outage = base(); outage.tokenExpiresAt = outage.now - 60; outage.lastOnlineCheck = outage.now - 60;
        CHECK(Decide(outage) == Verdict::Allowed);
        outage.lastOnlineCheck = outage.now - 7 * D - 1;
        CHECK(Decide(outage) == Verdict::GraceExpired);
        Inputs refused = outage; refused.signedIn = false; refused.lastOnlineCheck = refused.now;
        CHECK(Decide(refused) == Verdict::NeedSignIn);
    }

    // ---- What to store as the last-confirmed time (NextLastCheck) ----
    {
        const long N = 2000000000L;
        CHECK(NextLastCheck(0, N, 0, true) == N);                      // first unexpired token seen
        CHECK(NextLastCheck(N - 100, N, 0, true) == N - 100);          // fresh enough: no write
        CHECK(NextLastCheck(N - 601, N, 0, true) == N);                // stale: refresh it
        CHECK(NextLastCheck(N + 5000, N, 0, true) == N);               // stored time in the future (clock corrected): reset
        CHECK(NextLastCheck(N - 9 * D, N, 0, false) == N - 9 * D);     // expired token, no confirmation: nothing written
        CHECK(NextLastCheck(0, N, 0, false) == 0);
        CHECK(NextLastCheck(N - 9 * D, N, N - 5, false) == N - 5);     // server confirmed: counts even though the token looks expired (clock ahead / opaque token)
        CHECK(NextLastCheck(N - 5, N, N - 9 * D, false) == N - 5);     // an older confirmation never moves it back
        // Clock an hour+ fast: every token looks expired, but each sign-in/renewal is a confirmation, so the gate does not lock after 7 days.
        Inputs i = base(); i.now = N + 8 * D; i.tokenExpiresAt = i.now - 7200;
        i.lastOnlineCheck = NextLastCheck(N, i.now, i.now - 60, false);
        CHECK(Decide(i) == Verdict::Allowed);
        i.lastOnlineCheck = N;                                          // without the confirmation write it would lock
        CHECK(Decide(i) == Verdict::GraceExpired);
        // Opaque (non-JWT) token: expiry unknown, allowed while the server keeps confirming
        Inputs o = base(); o.tokenExpiresAt = 0; o.lastOnlineCheck = NextLastCheck(0, o.now, o.now - 1, false);
        CHECK(Decide(o) == Verdict::Allowed);
    }
    // First run after an upgrade: seeded from the session file's time (a file that exists is about "now"; a missing one is 0).
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_login_gate_seed";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
        CHECK(SeedLastCheckFromSession((dir / "session.txt").string()) == 0);
        { std::ofstream f(dir / "session.txt"); f << "a\nb\n"; }
        const long t = SeedLastCheckFromSession((dir / "session.txt").string());
        CHECK(t > 0 && std::labs(t - (long)std::time(nullptr)) < 120);
        fs::remove_all(dir, ec);
    }

    // ---- The switch ----
    CHECK(Required(nullptr, true, false));
    CHECK(!Required(nullptr, false, false));      // setting off
    CHECK(!Required("0", true, false));
    CHECK(!Required("false", true, false));
    CHECK(!Required("OFF", true, false));
    CHECK(Required("1", false, false));           // env forces on over a setting that is off
    CHECK(Required("yes", false, true));          // ...even in a demo mode
    CHECK(!Required(nullptr, true, true));        // demo hooks run without an account
    CHECK(Required("", true, false));             // empty = unset
    CHECK(Required("maybe", true, false));        // unknown word ignored: default stays ON
    CHECK(!Required("maybe", true, true));

    // ---- The last-check file ----
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_login_gate_test";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
        CHECK(ReadLastCheck(dir.string()) == 0);                      // missing file
        CHECK(WriteLastCheck(dir.string(), 1234567890L));
        CHECK(ReadLastCheck(dir.string()) == 1234567890L);
        { std::ofstream f(LastCheckFile(dir.string()), std::ios::trunc); f << "garbage\n"; }
        CHECK(ReadLastCheck(dir.string()) == 0);                      // unreadable = never
        { std::ofstream f(LastCheckFile(dir.string()), std::ios::trunc); f << "-7\n"; }
        CHECK(ReadLastCheck(dir.string()) == 0);                      // negative = never
        fs::remove_all(dir, ec);
    }

    TEST_MAIN_RESULT();
}
