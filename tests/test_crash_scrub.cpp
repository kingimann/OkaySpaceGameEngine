#include "test_framework.hpp"
#include "CrashScrub.hpp"
#include <string>

// The scrubber (shared/CrashScrub.hpp): hostile text in, no personal data out. Each block names what it is guarding.
using namespace okaycrash;

static ScrubContext Alice() {
    ScrubContext c;
    c.usernames = {"alice", "Alice Smith"};
    c.hostnames = {"ALICE-PC"};
    c.homeDirs = {"C:\\Users\\alice", "/home/alice"};
    return c;
}
static std::string S(const std::string& in) { return Scrub(in, Alice()); }
static bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

int main() {
    RUN_SUITE("crash_scrub");

    // ---- Windows user folders: any drive, any slash, any case, a name with spaces, doubled backslashes (JSON) ------------------------
    CHECK(S("Exe: C:\\Users\\alice\\AppData\\Local\\OkaySpace\\Tools\\OkayEngine.exe") == "Exe: %USERPROFILE%\\AppData\\Local\\OkaySpace\\Tools\\OkayEngine.exe");
    CHECK(S("D:\\USERS\\Bob\\Desktop\\a.png") == "%USERPROFILE%\\Desktop\\a.png");                 // another person on this PC, another drive, upper case
    CHECK(S("c:/users/carol/Documents/My Game/x.okayscene") == "%USERPROFILE%/Documents/My Game/x.okayscene");
    CHECK(S("C:\\Users\\John Smith\\Documents\\x") == "%USERPROFILE%\\Documents\\x");              // a name with a space
    CHECK(S("C:\\\\Users\\\\dave\\\\x.txt") == "%USERPROFILE%\\\\x.txt");                           // escaped for JSON
    CHECK(S("C:\\Documents and Settings\\erin\\Desktop") == "%USERPROFILE%\\Desktop");
    CHECK(S("opened 'C:\\Users\\frank\\x.png' ok") == "opened '%USERPROFILE%\\x.png' ok");
    CHECK(S("C:\\Users\\Zoë Müller\\x") == "%USERPROFILE%\\x");                                    // non-ASCII names
    CHECK(S("C:\\Users\\") == "C:\\Users\\" && S("C:\\Users") == "C:\\Users");                       // no name, nothing to take out
    CHECK(S("C:\\Program Files\\OkaySpace\\OkayEngine.exe") == "C:\\Program Files\\OkaySpace\\OkayEngine.exe");   // other folders stay
    CHECK(S("\\\\?\\C:\\Users\\gina\\x") == "\\\\?\\%USERPROFILE%\\x");                              // the \\?\ prefix
    // ---- Linux / macOS home folders ---------------------------------------------------------------------------------------------------
    CHECK(S("/home/alice/projects/space/a.scene") == "~/projects/space/a.scene");
    CHECK(S("Exe: /home/hank/.local/share/OkaySpace/OkayEngine") == "Exe: ~/.local/share/OkaySpace/OkayEngine");
    CHECK(S("/Users/ivy/Library/Application Support/x") == "~/Library/Application Support/x");
    CHECK(S("/mnt/c/Users/jack/x") == "/mnt/c/Users/<user>/x");                                      // inside a longer path the path stays
    CHECK(S("loaded /usr/lib/libfoo.so from /home/ then") == "loaded /usr/lib/libfoo.so from /home/ then");
    // ---- UNC paths --------------------------------------------------------------------------------------------------------------------
    CHECK(S("\\\\OFFICE-SRV01\\share\\game\\a.png") == "\\\\<host>\\share\\game\\a.png");
    CHECK(S("\\\\?\\GLOBALROOT\\x") == "\\\\?\\GLOBALROOT\\x" && S("\\\\.\\pipe\\foo") == "\\\\.\\pipe\\foo");

    // ---- This machine's own names -----------------------------------------------------------------------------------------------------
    CHECK(S("Loading for ALICE-PC (alice)") == "Loading for <host> (<user>)");
    CHECK(S("alice, ALICE, Alice Smith and malice") == "<user>, <user>, <user> and malice");        // whole words only, any case
    CHECK(S("D:\\Games\\alice\\x") == "D:\\Games\\<user>\\x");
    CHECK(S("C:\\Users\\alice\\x and C:/Users/alice/y") == "%USERPROFILE%\\x and %USERPROFILE%/y");
    {   // a login name that is a common word is not turned into <user> everywhere: "main" stays in the backtrace
        ScrubContext c; c.usernames = {"main", "ab", "admin"}; c.hostnames = {"test"};
        CHECK(Scrub("#00 OkayEngine.exe + 0x22  main +0x9 test ab admin", c) == "#00 OkayEngine.exe + 0x22  main +0x9 test ab admin");
        CHECK(Scrub("C:\\Users\\main\\x", c) == "%USERPROFILE%\\x");   // but inside a user folder it always goes
    }
    {   // the home folder of this machine, even in a form the path rules do not know
        ScrubContext c; c.homeDirs = {"E:\\Players\\Zed"};
        CHECK(Scrub("saved to E:\\Players\\Zed\\Saves and e:/players/zed/x", c) == "saved to %USERPROFILE%\\Saves and %USERPROFILE%/x");
    }

    // ---- Emails -----------------------------------------------------------------------------------------------------------------------
    CHECK(S("mail alice.smith+game@example.co.uk now") == "mail <email> now");
    CHECK(S("Contact:bob@x.io.") == "Contact:<email>.");
    CHECK(S("a@b a@b.c a@b.cc") == "a@b a@b.c <email>");                                           // a real domain needs a 2+ letter ending
    CHECK(S("git@github.com:owner/repo.git") == "<email>:owner/repo.git");
    CHECK(S("@alice and user@host and x@") == "@<user> and user@host and x@");
    CHECK(S("\xC3\xA9@dom.com") == "\xC3\xA9@dom.com" || S("\xC3\xA9@dom.com") == "<email>");       // non-ASCII local parts: never garbled
    // ---- Web addresses with a name and password ---------------------------------------------------------------------------------------
    CHECK(S("fetch https://alice:hunter2@example.com/path?x=1") == "fetch https://<redacted>@example.com/path?x=1");
    CHECK(S("see https://example.com/a@b/c and http://h:80/") == "see https://example.com/a@b/c and http://h:80/");

    // ---- Secrets ----------------------------------------------------------------------------------------------------------------------
    CHECK(S("token ghp_abcdefghijklmnopqrstuvwxyz0123456789 end") == "token <token> end");
    CHECK(S("github_pat_11ABCDEFG0abcdefghijkl_mnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUV") == "<token>");
    CHECK(S("key=sb_publishable_VYrh0rGNcTNTJRe1CovsoA_sP9ia4yn") == "key=<token>");
    CHECK(S("sb_secret_abcdefghijklmnop") == "<token>" && S("AKIAIOSFODNN7EXAMPLE") == "<token>" && S("xoxb-123456789012-abcdef") == "<token>");
    CHECK(S("sk-ant-api03-abcdefghijklmnopqrstuvwxyz and sk-abcdefghijklmnopqrstuvwxyz0123") == "<token> and <token>");
    CHECK(S("eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.abcDEF_ghi-123") == "<token>");     // a JSON web token
    CHECK(S("Authorization: Bearer abc.def.ghi\nnext line") == "Authorization: <redacted>\nnext line");
    CHECK(S("header was Bearer abcdefghijkl123456 and") == "header was Bearer <token> and");
    CHECK(S("password=hunter2 user=x") == "password=<redacted> user=x");
    CHECK(S("{\"api_key\": \"abc123\", \"name\": \"x\"}") == "{\"api_key\": \"<redacted>\", \"name\": \"x\"}");
    CHECK(S("ACCESS_TOKEN: abc123;next") == "ACCESS_TOKEN: <redacted>;next");
    CHECK(S("Token::Parse failed, secret = 'swordfish' ok") == "Token::Parse failed, secret = '<redacted>' ok");
    CHECK(S("OKAY_ACCOUNT_API_KEY=sb_publishable_abc") == "OKAY_ACCOUNT_API_KEY=<redacted>");
    CHECK(S("the secret of the game") == "the secret of the game" && S("Last step: Auth.Token") == "Last step: Auth.Token");   // words alone are not secrets
    CHECK(S("sha256 e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == "sha256 <token>");   // 64 hex
    CHECK(S("abcdef0123456789 0x7f1c2b3d4e5f") == "abcdef0123456789 0x7f1c2b3d4e5f");               // addresses and short hashes stay
    CHECK(S("aB3dE6gH9jK2mN5pQ8sT1vW4yZ7bC0eF3hJ6kM9nP") == "<token>");                              // 41 mixed letters and digits
    CHECK(S("_ZN4okay5Scene14UpdateTransformsEfRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE") ==
          "_ZN4okay5Scene14UpdateTransformsEfRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE");   // a mangled name is not a key
    CHECK(S("snake_case_identifier_that_is_quite_long_but_not_a_secret_at_all_1") == "snake_case_identifier_that_is_quite_long_but_not_a_secret_at_all_1");

    // ---- IP addresses -----------------------------------------------------------------------------------------------------------------
    CHECK(S("connect 203.0.113.7:7777 from 192.168.1.20") == "connect <ip>:7777 from <ip>");
    CHECK(S("local 127.0.0.1 any 0.0.0.0") == "local 127.0.0.1 any 0.0.0.0");
    CHECK(S("v2.874.0 build 10.0.19045.2006 and 999.1.1.1 and 1.2.3.4.5") == "v2.874.0 build 10.0.19045.2006 and 999.1.1.1 and 1.2.3.4.5");   // versions are not addresses
    CHECK(S("peer fe80::1ff:fe23:4567:890a and 2001:db8:85a3:0:0:8a2e:370:7334") == "peer <ip> and <ip>");
    CHECK(S("mac aa:bb:cc:dd:ee:ff") == "mac <ip>");
    CHECK(S("at 21:14:05 and 12:30:45:123, okay::Scene::Update and std::string") == "at 21:14:05 and 12:30:45:123, okay::Scene::Update and std::string");

    // ---- Markdown / control characters / line endings / huge input -----------------------------------------------------------------------
    CHECK(S(std::string("a\0b\x01\x1f" "c\r\nd", 10)) == "abc\nd");                                  // control characters and CR are dropped, newline and tab stay
    CHECK(S("tab\there") == "tab\there");
    CHECK(S("") == "" && S("   ") == "   ");
    CHECK(S("[click](javascript:alert(1)) <script>alert(1)</script> @everyone") == "[click](javascript:alert(1)) <script>alert(1)</script> @everyone");   // not this layer's job (the issue script handles it)
    {   // idempotent: scrubbing scrubbed text changes nothing
        const std::string dirty = "C:\\Users\\alice\\x alice@x.com 10.1.2.3 password=abc ghp_abcdefghijklmnopqrstuvwxyz0123 ALICE-PC Bearer abcdefghijkl /home/alice/p";
        const std::string once = S(dirty);
        CHECK(!Has(once, "alice@x") && !Has(once, "10.1.2.3") && !Has(once, "ghp_") && !Has(once, "ALICE-PC") && !Has(once, "abc "));
        CHECK(S(once) == once);
    }
    {   // a big report scrubs in reasonable time and keeps its size order
        std::string big;
        for (int i = 0; i < 4000; ++i) big += "[-" + std::to_string(i) + "s] I Loaded C:\\Users\\alice\\assets\\tex" + std::to_string(i) + ".png (12 ms) from 10.0." + std::to_string(i % 200) + ".5\n";
        const std::string out = S(big);
        CHECK(!Has(out, "alice") && !Has(out, "10.0.") && out.size() < big.size());
        CHECK(Has(out, "[-3999s] I Loaded %USERPROFILE%\\assets\\tex3999.png (12 ms) from <ip>"));
    }
    {   // a long run of one character, a long run of '@', of separators and of colons does not hang or blow up
        const std::string a(200000, 'a'), at(50000, '@'), sep(50000, '\\'), col(50000, ':'), dots(50000, '.');
        CHECK(S(a).size() <= a.size() && S(at).size() <= at.size() * 8 && S(sep) == sep && S(col) == col && S(dots) == dots);
        CHECK(S("C:" + sep + "Users" + sep).size() > 0);
    }

    // ---- The context from the environment ---------------------------------------------------------------------------------------------
    {
        const ScrubContext c = LocalContext();   // whatever this machine has: must not crash and must scrub its own home folder when it has one
        if (!c.homeDirs.empty() && c.homeDirs[0].size() > 4) CHECK(!Has(Scrub("x " + c.homeDirs[0] + "/y", c), c.homeDirs[0]));
    }

    // ---- LimitLines ----------------------------------------------------------------------------------------------------------------------
    CHECK(LimitLines("short\nline", 10) == "short\nline");
    CHECK(LimitLines(std::string(50, 'x') + "\nok\n", 10) == std::string(10, 'x') + "...[cut]\nok\n");
    CHECK(LimitLines("", 5).empty());

    TEST_MAIN_RESULT();
}
