// Tests for the launcher's local account backend (register / login / hashing /
// session persistence). The remote backend isn't exercised here since it needs
// a live auth server.
#include "test_framework.hpp"
#include "okay/Platform/Account/AccountService.hpp"

#include <filesystem>
#include <fstream>
#include <set>

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

    // ---- PBKDF2-HMAC-SHA256 (the local password hash) ----
    {
        auto hex = [](const std::array<std::uint8_t, 32>& d) { return acct::detail::ToHex(d.data(), d.size()); };
        const std::vector<std::uint8_t> salt = {'s', 'a', 'l', 't'};
        // Vectors that match Python's hashlib.pbkdf2_hmac('sha256', ...) and RFC 7914 section 11's HMAC-SHA-256 PBKDF2 tests.
        CHECK(hex(acct::detail::Pbkdf2Sha256("password", salt, 1)) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
        CHECK(hex(acct::detail::Pbkdf2Sha256("password", salt, 2)) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
        CHECK(hex(acct::detail::Pbkdf2Sha256("password", salt, 4096)) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
        // A key longer than the block size (hashed first) and a binary salt.
        CHECK(hex(acct::detail::Pbkdf2Sha256(std::string(100, 'p'), {0x00, 0xff, 0x10}, 50)) == "ac61021deea2dc2110ec112f9315718f71d4d250066873eac4fc93f7a3dcffc5");
        // The stored form is versioned, salted, and never holds the password.
        const std::string saltHex = acct::detail::RandomHex(16);
        const std::string v2 = acct::detail::HashPasswordV2("hunter2", saltHex, 1000);
        CHECK(v2.rfind("$pbkdf2-sha256$1000$", 0) == 0 && v2.size() == 20 + 64 && v2.find("hunter2") == std::string::npos);
        CHECK(v2 == acct::detail::HashPasswordV2("hunter2", saltHex, 1000));
        CHECK(v2 != acct::detail::HashPasswordV2("hunter2", acct::detail::RandomHex(16), 1000));   // another salt
        CHECK(v2 != acct::detail::HashPasswordV2("hunter3", saltHex, 1000));
        std::uint32_t rounds = 0;
        CHECK(acct::detail::StoredRounds(v2, rounds) && rounds == 1000);
        CHECK(!acct::detail::StoredRounds("$pbkdf2-sha256$5$00", rounds));                      // too few rounds claimed
        CHECK(!acct::detail::StoredRounds("$pbkdf2-sha256$99999999999$00", rounds));            // absurd
        CHECK(!acct::detail::StoredRounds("$pbkdf2-sha256$12x$00", rounds));
        CHECK(!acct::detail::StoredRounds(std::string(64, 'a'), rounds));                       // the old chain is not v2
        // Salts and tokens come from the OS CSPRNG: distinct, right length, not a seeded sequence.
        std::set<std::string> seen;
        for (int i = 0; i < 64; ++i) seen.insert(acct::detail::RandomHex(16));
        CHECK(seen.size() == 64 && seen.begin()->size() == 32);
        std::uint8_t rb[32] = {0};
        CHECK(acct::detail::OsRandom(rb, sizeof(rb)));
        int nonzero = 0; for (std::uint8_t b : rb) nonzero += b != 0;
        CHECK(nonzero > 8);
    }
    // ---- an account made by an older version (SHA-256 chain) signs in and is upgraded on that sign-in ----
    {
        fs::path odir = fs::temp_directory_path() / ("okay_acct_legacy_" + acct::detail::RandomHex(8));
        fs::remove_all(odir);
        fs::create_directories(odir);
        const std::string salt = acct::detail::RandomHex(16), legacy = acct::detail::HashPassword("oldpass1", salt);
        { std::ofstream(odir / "accounts.db") << "olduser\t" << salt << "\t" << legacy << "\n"; }
        acct::AccountService svc(odir);
        CHECK(!svc.Login("olduser", "wrongpass").ok);
        { std::ifstream f(odir / "accounts.db"); std::string l; std::getline(f, l); CHECK(l.find(legacy) != std::string::npos); }   // a failed login changes nothing
        CHECK(svc.Login("olduser", "oldpass1").ok);
        std::string line;
        { std::ifstream f(odir / "accounts.db"); std::getline(f, line); }
        CHECK(line.find("$pbkdf2-sha256$600000$") != std::string::npos && line.find(legacy) == std::string::npos && line.find(salt) == std::string::npos);
        svc.Logout();
        CHECK(svc.Login("olduser", "oldpass1").ok && !svc.Login("olduser", "oldpass2").ok);   // the upgraded record works
        // A record made when the cost was lower is raised on the next sign-in.
        const std::string s2 = acct::detail::RandomHex(16);
        { std::ofstream(odir / "accounts.db") << "weak\t" << s2 << "\t" << acct::detail::HashPasswordV2("weakpass1", s2, 2000) << "\n"; }
        acct::AccountService svc2(odir);
        CHECK(svc2.Login("weak", "weakpass1").ok);
        { std::ifstream f(odir / "accounts.db"); std::getline(f, line); }
        CHECK(line.find("$pbkdf2-sha256$600000$") != std::string::npos);
        // A damaged v2 record never signs anyone in.
        { std::ofstream(odir / "accounts.db") << "bad\t" << s2 << "\t$pbkdf2-sha256$zz$00\n"; }
        acct::AccountService svc3(odir);
        CHECK(!svc3.Login("bad", "anything").ok);
        fs::remove_all(odir);
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

        // VerifyToken: an empty token is rejected with no network call; the local
        // backend never verifies. (A real token is checked against auth/v1/user.)
        std::string uid = "x";
        CHECK(!sb.VerifyToken("", uid));            // empty -> false, clears uid
        CHECK(uid.empty());
        acct::AccountService localSvc(dir, "", "");
        CHECK(!localSvc.VerifyToken("anything", uid));   // offline/local -> false
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
        // Reset email: the recover URL carries the (percent-encoded) landing page.
        CHECK(acct::AccountService::RecoverUrl("https://x.supabase.co/", "") ==
              "https://x.supabase.co/auth/v1/recover");
        CHECK(acct::AccountService::RecoverUrl("https://x.supabase.co", "https://a.io/reset/?v=1") ==
              "https://x.supabase.co/auth/v1/recover?redirect_to=https%3A%2F%2Fa.io%2Freset%2F%3Fv%3D1");
        CHECK(acct::AccountService::RecoverUrl("https://x.supabase.co", "javascript:alert(1)") ==
              "https://x.supabase.co/auth/v1/recover");   // non-http(s) redirects are dropped
        {
            acct::AccountService r2(dir);
            CHECK(!r2.HasResetRedirect());
            r2.SetResetRedirect("https://a.io/reset/");
            CHECK(r2.HasResetRedirect());
        }
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

    // ---- server JSON: escapes are decoded ----
    // Supabase's auth server writes '&', '<' and '>' as \u0026 / \u003c / \u003e,
    // so a username like "Tom&Jerry" must not come back as "Tomu0026Jerry".
    {
        const std::string tokenReply =
            "{\"access_token\":\"a.b.c\",\"user\":{\"user_metadata\":{\"username\":\"Tom\\u0026Jerry \\u003c3\"}}}";
        CHECK(acct::detail::JsonField(tokenReply, "username") == "Tom&Jerry <3");
        CHECK(acct::detail::JsonField(tokenReply, "access_token") == "a.b.c");
        CHECK(acct::detail::JsonField("{\"data\":\"line1\\nline2\\t\\\"q\\\" \\\\ \\/\"}", "data") ==
              "line1\nline2\t\"q\" \\ /");
        CHECK(acct::detail::JsonField("{\"n\":\"caf\\u00e9 \\ud83d\\ude00\"}", "n") == "caf\xC3\xA9 \xF0\x9F\x98\x80");
        auto keys = acct::detail::JsonStringArray("{\"keys\":[\"a\\u0026b\",\"c\\nd\"]}", "keys");
        CHECK(keys.size() == 2 && keys[0] == "a&b" && keys[1] == "c\nd");
        auto names = acct::detail::JsonFieldValues("[{\"name\":\"x\\u003ey\"},{\"name\":\"z\"}]", "name");
        CHECK(names.size() == 2 && names[0] == "x>y" && names[1] == "z");
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
