#include "test_framework.hpp"
#include "CrashScrub.hpp"
#include <cstdlib>
#include <string>

#if defined(_WIN32)
static void SetEnv(const char* k, const char* v) { _putenv_s(k, v); }
#else
static void SetEnv(const char* k, const char* v) { setenv(k, v, 1); }
#endif

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
    // ---- Web addresses with a name and password ---------------------------------------------------------------------------------------   // lint-allow: secret (a made-up value the scrubber must catch)
    CHECK(S("fetch https://alice:hunter2@example.com/path?x=1") == "fetch https://<redacted>@example.com/path?x=1");
    CHECK(S("see https://example.com/a@b/c and http://h:80/") == "see https://example.com/a@b/c and http://h:80/");

    // ---- Secrets ----------------------------------------------------------------------------------------------------------------------   // lint-allow: secret (a made-up value the scrubber must catch)
    CHECK(S("token ghp_abcdefghijklmnopqrstuvwxyz0123456789 end") == "token <token> end");   // lint-allow: secret (a made-up value the scrubber must catch)
    CHECK(S("github_pat_11ABCDEFG0abcdefghijkl_mnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUV") == "<token>");   // lint-allow: secret (a made-up value the scrubber must catch)
    CHECK(S("key=sb_publishable_TESTONLY00000000000000000000000") == "key=<token>");
    CHECK(S("sb_secret_abcdefghijklmnop") == "<token>" && S("AKIAIOSFODNN7EXAMPLE") == "<token>" && S("xoxb-123456789012-abcdef") == "<token>");   // lint-allow: secret (a made-up value the scrubber must catch)
    CHECK(S("sk-ant-api03-abcdefghijklmnopqrstuvwxyz and sk-abcdefghijklmnopqrstuvwxyz0123") == "<token> and <token>");   // lint-allow: secret (a made-up value the scrubber must catch)
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
    CHECK(S("snake_case_identifier_that_is_quite_long_but_not_a_secret_at_all_1") == "snake_case_identifier_that_is_quite_long_but_not_a_secret_at_all_1");   // lint-allow: secret (a made-up value the scrubber must catch)

    // ---- IP addresses -----------------------------------------------------------------------------------------------------------------
    CHECK(S("connect 203.0.113.7:7777 from 192.168.1.20") == "connect <ip>:7777 from <ip>");
    CHECK(S("local 127.0.0.1 any 0.0.0.0") == "local 127.0.0.1 any 0.0.0.0");
    CHECK(S("v2.874.0 build 10.0.19045.2006 and 999.1.1.1 and 1.2.3.4.5") == "v2.874.0 build 10.0.19045.2006 and 999.1.1.1 and 1.2.3.4.5");   // versions are not addresses
    CHECK(S("peer fe80::1ff:fe23:4567:890a and 2001:db8:85a3:0:0:8a2e:370:7334") == "peer <ip> and <ip>");
    CHECK(S("mac aa:bb:cc:dd:ee:ff") == "mac <mac>");
    CHECK(S("at 21:14:05 and 12:30:45:123, okay::Scene::Update and std::string") == "at 21:14:05 and 12:30:45:123, okay::Scene::Update and std::string");

    // ---- Markdown / control characters / line endings / huge input -----------------------------------------------------------------------
    CHECK(S(std::string("a\0b\x01\x1f" "c\r\nd", 10)) == "abc\nd");                                  // control characters and CR are dropped, newline and tab stay
    CHECK(S("tab\there") == "tab\there");
    CHECK(S("") == "" && S("   ") == "   ");
    CHECK(S("[click](javascript:alert(1)) <script>alert(1)</script> @everyone") == "[click](javascript:alert(1)) <script>alert(1)</script> @everyone");   // not this layer's job (the issue script handles it)
    {   // idempotent: scrubbing scrubbed text changes nothing
        const std::string dirty = "C:\\Users\\alice\\x alice@x.com 10.1.2.3 password=abc ghp_abcdefghijklmnopqrstuvwxyz0123 ALICE-PC Bearer abcdefghijkl /home/alice/p";   // lint-allow: secret (a made-up value the scrubber must catch)
        const std::string once = S(dirty);
        CHECK(!Has(once, "alice@x") && !Has(once, "10.1.2.3") && !Has(once, "ghp_") && !Has(once, "ALICE-PC") && !Has(once, "abc "));
        CHECK(S(once) == once);
    }
    {   // found by fuzzing: taking a token out exposed a network path next to it; the rules run again until nothing changes
        const std::string t = "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxIn0.abc0\\\\HOST\\share";   // lint-allow: secret (a made-up token shape)
        const std::string once = S(t);
        CHECK(once == "<token>\\\\<host>\\share" && S(once) == once);
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

    // ---- Leaks the security review found (2026-11-15) -----------------------------------------------------------------------------------
    // A name inside a longer word: after '_', a digit or a camelCase step it is still the name; inside a lower-case word it is not ("malice").
    CHECK(S("alice_project.scene") == "<user>_project.scene");
    CHECK(S("AliceGame") == "<user>Game" && S("MyAliceGame.okayscene") == "My<user>Game.okayscene" && S("my_alice_save") == "my_<user>_save");
    CHECK(S("D:\\Games\\alice_stuff\\a.png") == "D:\\Games\\<user>_stuff\\a.png");
    CHECK(S("alice123 and alice2024_final and 2024alice") == "<user>123 and <user>2024_final and 2024<user>");
    CHECK(S("malice Malice MALICE alicia aliceland ALICEGAME") == "malice Malice MALICE alicia aliceland ALICEGAME");   // not the name, or no way to tell
    CHECK(S("ALICE-PC_backup and ALICE-PCs") == "<host>_backup and <user>-PCs");   // (ALICE-PCs is not the computer's name, but it starts with the login name)
    {   // a surname: the home folder's own name (here a full name, "C:\Users\Alice Smith") is a name of this person too
        ScrubContext c; c.usernames = {"alice"}; c.homeDirs = {"C:\\Users\\Alice Smith"};
        CHECK(Scrub("Hello Alice Smith and alice", c) == "Hello <user> and <user>");   // (before: "Hello <user> Smith and <user>")
        CHECK(Scrub("alice smith's game, ALICE SMITH", c) == "<user>'s game, <user>");
        std::string saved[4]; const char* names[4] = {"USERNAME", "USER", "LOGNAME", "USERPROFILE"};
        for (int i = 0; i < 4; ++i) { const char* v = std::getenv(names[i]); saved[i] = v ? v : ""; }
        SetEnv("USERNAME", "alice"); SetEnv("USER", "alice"); SetEnv("LOGNAME", "alice"); SetEnv("USERPROFILE", "C:\\Users\\Alice Smith");
        const ScrubContext lc = LocalContext();   // from the environment of this process
        for (int i = 0; i < 4; ++i) SetEnv(names[i], saved[i].c_str());
        CHECK(Scrub("Hello Alice Smith and alice, Alice Smith's game", lc) == "Hello <user> and <user>, <user>'s game");
        CHECK(Scrub("saved by Alice Smith to C:\\Users\\Alice Smith\\Documents\\x", lc) == "saved by <user> to %USERPROFILE%\\Documents\\x");
    }
    {   // a home folder called Users, root or home says nothing about the person
        ScrubContext c; c.homeDirs = {"C:\\Users", "/root", "/home"};
        CHECK(Scrub("the users of root at home", c) == "the users of root at home");
        ScrubContext z; z.homeDirs = {"/home/zelda"};
        CHECK(Scrub("welcome zelda", z) == "welcome <user>");
    }
    // OneDrive's folder carries the employer's name.
    CHECK(S("C:\\Users\\bob\\OneDrive - Acme Corp\\Documents\\x.scene") == "%USERPROFILE%\\OneDrive - <org>\\Documents\\x.scene");
    CHECK(S("saved to D:\\OneDrive - Contoso Ltd\\Games and e:/onedrive - Initech/x") == "saved to D:\\OneDrive - <org>\\Games and e:/OneDrive - <org>/x");
    CHECK(S("OneDrive\\Documents and OneDrive - ") == "OneDrive\\Documents and OneDrive - ");
    // MAC addresses with dashes, and GUIDs.
    CHECK(S("adapter AA-BB-CC-DD-EE-FF up, aa-bb-cc-dd-ee-ff and 00:1A:2B:3C:4D:5E") == "adapter <mac> up, <mac> and <mac>");
    CHECK(S("device {3F2504E0-4F89-11D3-9A0C-0305E82C3301} and 3f2504e0-4f89-11d3-9a0c-0305e82c3301.") == "device <guid> and <guid>.");
    CHECK(S("2026-11-15 and 12-11-20 and 1-2-3-4-5-6 and 10-20-30-40-50 and a1b2-c3d4") == "2026-11-15 and 12-11-20 and 1-2-3-4-5-6 and 10-20-30-40-50 and a1b2-c3d4");
    // \\?\UNC\server\share
    CHECK(S("loaded \\\\?\\UNC\\fileserver01\\share\\x.png and \\\\?\\unc\\Srv\\s") == "loaded \\\\?\\UNC\\<host>\\share\\x.png and \\\\?\\unc\\<host>\\s");
    CHECK(S("\\\\?\\C:\\Windows\\x and \\\\.\\pipe\\foo") == "\\\\?\\C:\\Windows\\x and \\\\.\\pipe\\foo");
    // The home folder is only replaced as a whole folder: an 8.3 short name is still a user folder (the user-folder rule takes it).
    {
        ScrubContext c; c.homeDirs = {"C:\\Users\\alice"}; c.usernames = {"alice"};
        CHECK(Scrub("C:\\Users\\ALICES~1\\Desktop\\x.png", c) == "%USERPROFILE%\\Desktop\\x.png");
        CHECK(Scrub("C:\\Users\\alice\\x and C:\\Users\\alice", c) == "%USERPROFILE%\\x and %USERPROFILE%");
        ScrubContext d; d.homeDirs = {"E:\\Players\\Zed"};
        CHECK(Scrub("E:\\Players\\Zed\\Saves and E:\\Players\\Zed2\\x", d) == "%USERPROFILE%\\Saves and E:\\Players\\<user>2\\x");   // another folder, still carrying the name
    }
    // Invisible characters: the Unicode tag block (hidden text that a program reading the report can see), direction overrides, zero-width
    // characters, the byte order mark and the soft hyphen go; ordinary non-ASCII text stays.
    CHECK(S("a\xF3\xA0\x81\x81" "b\xF3\xA0\x80\x81\xF3\xA0\x81\xBF" "c") == "abc");                        // U+E0041, U+E0001, U+E007F
    CHECK(S("a\xE2\x80\xAE" "b\xE2\x80\x8B" "c\xE2\x81\xA6" "d\xEF\xBB\xBF" "e\xC2\xAD" "f\xE2\x80\x8D" "g") == "abcdefg");   // U+202E U+200B U+2066 U+FEFF U+00AD U+200D
    CHECK(S("caf\xC3\xA9 \xE2\x82\xAC \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x98\x80") == "caf\xC3\xA9 \xE2\x82\xAC \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x98\x80");
    CHECK(S("\xF3\xA0\x81") == "\xF3\xA0\x81");   // (a cut-off sequence is not a tag character; the sender's UTF-8 cleaning deals with it)
    {   // none of it is idempotence-breaking
        const std::string t = "AA-BB-CC-DD-EE-FF {3F2504E0-4F89-11D3-9A0C-0305E82C3301} \\\\?\\UNC\\srv\\s C:\\Users\\bob\\OneDrive - Acme\\x alice_project AliceGame";
        const std::string once = S(t);
        CHECK(once.find("AA-BB") == std::string::npos && once.find("3F2504E0") == std::string::npos && once.find("srv") == std::string::npos &&
              once.find("Acme") == std::string::npos && once.find("alice") == std::string::npos && once.find("Alice") == std::string::npos);
        CHECK(S(once) == once);
    }
    {   // and still linear on hostile runs of every new shape
        const std::string dash(200000, '-'), hex2("aa-"), od("OneDrive - ");
        std::string many; for (int i = 0; i < 30000; ++i) many += "aa-";
        std::string guids; for (int i = 0; i < 6000; ++i) guids += "3f2504e0-4f89-11d3-9a0c-0305e82c33";
        std::string drives; for (int i = 0; i < 15000; ++i) drives += "OneDrive - ";
        std::string unc; for (int i = 0; i < 20000; ++i) unc += "\\\\?\\UNC\\";
        CHECK(S(dash).size() <= dash.size() && S(many).size() <= many.size() * 2 && S(guids).size() <= guids.size() * 2 && S(drives).size() <= drives.size() * 2 && S(unc).size() <= unc.size() * 2);
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
