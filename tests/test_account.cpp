// Tests for the launcher's local account backend (register / login / hashing /
// session persistence). The remote backend isn't exercised here since it needs
// a live auth server.
#include "test_framework.hpp"
#include "Account.hpp"

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

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
