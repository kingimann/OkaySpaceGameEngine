// Tests for the launcher's local account backend (register / login / hashing /
// session persistence). The remote backend isn't exercised here since it needs
// a live auth server.
#include "test_framework.hpp"
#include "okay/Platform/Account/AccountService.hpp"

#include <filesystem>

namespace fs = std::filesystem;
namespace acct = okay::account;

int main() {
    RUN_SUITE("account");

    // Use a throwaway config directory so we never touch the real one.
    fs::path dir = fs::temp_directory_path() /
                   ("okay_acct_test_" + acct::detail::RandomHex(8));
    fs::remove_all(dir);

    // ---- password hashing ----
    {
        std::string salt = "deadbeef";
        std::string h1 = acct::detail::HashPassword("hunter2", salt);
        std::string h2 = acct::detail::HashPassword("hunter2", salt);
        std::string h3 = acct::detail::HashPassword("hunter2", "feedface");
        CHECK(h1 == h2);                 // deterministic for the same salt
        CHECK(h1 != h3);                 // salt changes the result
        CHECK(h1.size() == 64);          // SHA-256 hex
        CHECK(h1.find("hunter2") == std::string::npos);  // never plaintext
        // Known SHA-256 vector ("abc").
        CHECK(acct::detail::Sha256Hex("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }

    // ---- register + login (local backend) ----
    {
        acct::AccountService svc(dir);
        CHECK(!svc.IsOnline());
        CHECK(!svc.IsLoggedIn());

        // Validation rejects bad input.
        CHECK(!svc.Register("ab", "longenough").ok);     // username too short
        CHECK(!svc.Register("alice", "123").ok);          // password too short

        acct::Result reg = svc.Register("alice", "s3cret!");
        CHECK(reg.ok);
        CHECK(svc.IsLoggedIn());
        CHECK(svc.CurrentSession().username == "alice");
        CHECK(!svc.CurrentSession().token.empty());

        // Duplicate usernames (case-insensitive) are rejected.
        CHECK(!svc.Register("ALICE", "another").ok);

        svc.Logout();
        CHECK(!svc.IsLoggedIn());

        // Wrong password fails; correct password works.
        CHECK(!svc.Login("alice", "wrong").ok);
        CHECK(!svc.Login("nobody", "s3cret!").ok);
        acct::Result login = svc.Login("alice", "s3cret!");
        CHECK(login.ok);
        CHECK(svc.IsLoggedIn());

        // Local backend: a session is always "valid" (nothing to check), and
        // authenticated requests aren't reached (no server).
        CHECK(svc.VerifySession());
        acct::ApiResponse r = svc.Api("/profile");
        CHECK(!r.reached);
        CHECK(!r.ok);

        // Cloud saves need a server, so they no-op on the local backend.
        CHECK(!svc.CloudSave("slot1", "level=3"));
        CHECK(svc.CloudLoad("slot1").empty());
        CHECK(!svc.CloudHas("slot1"));
        CHECK(svc.CloudList().empty());
        // Bad keys are rejected outright.
        CHECK(!svc.CloudSave("../etc/passwd", "x"));

        // Leaderboards also need a server; no-op on the local backend.
        CHECK(!svc.LeaderboardSubmit("high", 100));
        CHECK(svc.LeaderboardTop("high").empty());
    }

    // ---- backend selection (no network) ----
    {
        // Nothing configured -> local.
        acct::AccountService local(dir);
        CHECK(!local.IsOnline());
        CHECK(std::string(local.ProviderName()) == "local");
        CHECK(!local.UsesEmail());

        // URL only -> our custom server.
        acct::AccountService custom(dir, "https://accounts.example.com");
        CHECK(custom.IsOnline());
        CHECK(std::string(custom.ProviderName()) == "custom");
        CHECK(!custom.UsesEmail());

        // URL + API key -> managed (Supabase), identified by email.
        acct::AccountService sb(dir, "https://proj.supabase.co", "anon-key");
        CHECK(sb.IsOnline());
        CHECK(std::string(sb.ProviderName()) == "supabase");
        CHECK(sb.UsesEmail());

        // Supabase validates the identifier as an email before any network call.
        acct::Result bad = sb.Register("notanemail", "longenough");
        CHECK(!bad.ok);
        CHECK(bad.error.find("email") != std::string::npos);
        // Short password is caught locally too (no network).
        CHECK(!sb.Login("user@example.com", "123").ok);
    }

    // ---- session persists across service instances ----
    {
        acct::AccountService svc(dir);
        CHECK(svc.IsLoggedIn());
        CHECK(svc.CurrentSession().username == "alice");

        svc.Logout();
        acct::AccountService fresh(dir);
        CHECK(!fresh.IsLoggedIn());      // logout cleared the saved session
    }

    // ---- change password (local) + reset gating ----
    {
        acct::AccountService svc(dir);
        CHECK(svc.Login("alice", "s3cret!").ok);
        CHECK(svc.ChangePassword("newpass1").ok);
        svc.Logout();
        CHECK(!svc.Login("alice", "s3cret!").ok);   // old password no longer works
        CHECK(svc.Login("alice", "newpass1").ok);   // new password works
        // Password reset is a Supabase-only feature.
        CHECK(!svc.RequestPasswordReset("a@b.com").ok);
        // 3-arg register (username is ignored on the local backend).
        CHECK(svc.Register("bob", "passbob", "Bobby").ok);

        // Change username = rename the local account; email change is online-only.
        CHECK(svc.Login("alice", "newpass1").ok);
        CHECK(svc.ChangeUsername("alice2").ok);
        CHECK(svc.CurrentSession().username == "alice2");
        CHECK(!svc.ChangeUsername("bob").ok);        // taken
        CHECK(!svc.ChangeEmail("x@y.com").ok);       // Supabase only
        svc.Logout();
        CHECK(!svc.Login("alice", "newpass1").ok);   // old name gone
        CHECK(svc.Login("alice2", "newpass1").ok);   // new name works
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
