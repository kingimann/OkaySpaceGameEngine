// Browser step 2: running an OkaySpace web game from a page natively. The JSON reader the pack's manifest is read with
// (okay/Core/Json.hpp, against RFC 8259's rules), the web game pack reader (okay/Browser/GamePack.hpp: a good pack, gzip and
// plain, a truncated pack, an oversized one, "../" and other names that must not be written, a damaged gzip, duplicate
// names, unpacking into a folder) and the launch rules (okay/Browser/GameLaunch.hpp: which elements may run a game, the
// same-origin rule, the per-site folders, the player's command line, the web export's config in a page).
// Packs are built here the way scripts/web_package.py builds them (the test does not depend on the code it tests).
#include "test_framework.hpp"
#include "okay/Browser/GameLaunch.hpp"
#include "okay/Browser/GamePack.hpp"
#include "okay/Browser/Html.hpp"
#include "okay/Core/Inflate.hpp"   // detail::Crc32 for the gzip trailer of the test packs
#include "okay/Core/Json.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace okay::browser;
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

// ---- building packs ----------------------------------------------------------------------------------------------------

static std::string Esc(const std::string& s) {   // enough of a JSON string for the test names (quotes and backslashes)
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o;
}

// The pack bytes for [(path, data)], with the manifest as written by web_package.py (compact JSON).
static Bytes MakePack(const std::vector<std::pair<std::string, std::string>>& files, const std::string& manifestOverride = "") {
    std::string manifest = "{\"files\":[";
    std::string blob;
    for (std::size_t i = 0; i < files.size(); ++i) {
        manifest += (i ? "," : "") + std::string("{\"path\":\"") + Esc(files[i].first) + "\",\"offset\":" + std::to_string(blob.size()) +
                    ",\"size\":" + std::to_string(files[i].second.size()) + "}";
        blob += files[i].second;
    }
    manifest += "]}";
    if (!manifestOverride.empty()) manifest = manifestOverride;
    Bytes b = {'O', 'K', 'W', 'P', 'A', 'C', 'K', '1'};
    const std::uint32_t n = (std::uint32_t)manifest.size();
    for (int k = 0; k < 4; ++k) b.push_back((std::uint8_t)(n >> (8 * k)));
    b.insert(b.end(), manifest.begin(), manifest.end());
    b.insert(b.end(), blob.begin(), blob.end());
    return b;
}

// gzip (RFC 1952) with stored DEFLATE blocks (RFC 1951 section 3.2.4): no compressor needed in the test.
static Bytes Gzip(const Bytes& data) {
    Bytes g = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff};
    std::size_t at = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, data.size() - at);
        const bool last = at + n == data.size();
        g.push_back(last ? 1 : 0);
        g.push_back((std::uint8_t)(n & 0xff)); g.push_back((std::uint8_t)(n >> 8));
        g.push_back((std::uint8_t)(~n & 0xff)); g.push_back((std::uint8_t)((~n >> 8) & 0xff));
        g.insert(g.end(), data.begin() + (long)at, data.begin() + (long)(at + n));
        at += n;
    } while (at < data.size());
    const std::uint32_t crc = okay::inflate::detail::Crc32(data.data(), data.size());
    const std::uint32_t size = (std::uint32_t)data.size();
    for (int k = 0; k < 4; ++k) g.push_back((std::uint8_t)(crc >> (8 * k)));
    for (int k = 0; k < 4; ++k) g.push_back((std::uint8_t)(size >> (8 * k)));
    return g;
}

static bool Read(const Bytes& b, GamePack& p, std::string& why, const GamePackLimits& lim = GamePackLimits()) {
    return ReadGamePack(b.data(), b.size(), p, why, lim);
}

static bool Refused(const Bytes& b, const char* what, const GamePackLimits& lim = GamePackLimits()) {
    GamePack p;
    std::string why;
    if (Read(b, p, why, lim)) { std::cerr << "  accepted, should refuse: " << what << "\n"; return false; }
    if (why.empty()) { std::cerr << "  refused without a reason: " << what << "\n"; return false; }
    if (!p.files.empty() || !p.bytes.empty()) { std::cerr << "  refused but left data: " << what << "\n"; return false; }
    return true;
}

static std::string FileText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main() {
    RUN_SUITE("browser game");

    // ---- JSON (RFC 8259) ----
    {
        namespace J = okay::json;
        J::Value v;
        std::string why;
        CHECK(J::Parse(" {\"a\": [1, -2.5e3, true, false, null, \"x\\u00e9\\ud83d\\ude00\\n\"], \"b\": {}} ", v, &why));
        CHECK(v.IsObject() && v.Get("a") && v.Get("a")->array.size() == 6);
        if (v.Get("a") && v.Get("a")->array.size() == 6) {
            const auto& a = v.Get("a")->array;
            CHECK(a[0].number == 1.0 && a[1].number == -2500.0 && a[2].boolean && !a[3].boolean && a[4].IsNull());
            CHECK(a[5].string == "x\xc3\xa9\xf0\x9f\x98\x80\n");
        }
        CHECK(v.Get("b") && v.Get("b")->IsObject() && !v.Get("c"));
        std::uint64_t u = 0;
        CHECK(J::Parse("9007199254740992", v) && v.AsIndex(u) && u == 9007199254740992ull);
        CHECK(J::Parse("1.5", v) && !v.AsIndex(u));
        CHECK(J::Parse("-1", v) && !v.AsIndex(u));
        for (const char* bad : {"", "[1,]", "{\"a\":1,}", "01", "1.", ".5", "+1", "NaN", "'x'", "\"a\nb\"", "\"\\x\"", "\"\\ud800\"",
                                "\"\\udc00\"", "[1] 2", "{a:1}", "tru", "1e999", "[", "\"abc"}) {
            if (J::Parse(bad, v, &why)) { std::cerr << "  JSON accepted: " << bad << "\n"; CHECK(false); }
            else CHECK(!why.empty() && v.IsNull());
        }
        std::string deep(1000, '[');
        CHECK(!J::Parse(deep + std::string(1000, ']'), v, &why) && why.find("deep") != std::string::npos);
        J::Limits small;
        small.maxBytes = 4;
        CHECK(!J::Parse("[1,2,3]", v, &why, small));
    }

    // ---- a good pack, gzip and plain ----
    const std::vector<std::pair<std::string, std::string>> game = {
        {"/game.okayconfig", "title=Test Game\nwidth=320\nheight=200\n"},
        {"/game.okayscene", "scene data"},
        {"/assets/sprites/hero.png", std::string("\x89PNG\r\n\x1a\n\0\0", 10)},
        {"/empty.txt", ""},
    };
    const Bytes plain = MakePack(game);
    const Bytes packed = Gzip(plain);
    {
        GamePack p;
        std::string why;
        CHECK(Read(packed, p, why));
        CHECK(why.empty());
        CHECK(p.files.size() == 4);
        if (p.files.size() == 4) {
            CHECK(p.files[0].path == "game.okayconfig" && p.files[2].path == "assets/sprites/hero.png" && p.files[3].size == 0);
            CHECK(std::string((const char*)p.bytes.data() + p.files[1].offset, p.files[1].size) == "scene data");
            CHECK(std::string((const char*)p.bytes.data() + p.files[2].offset, p.files[2].size) == game[2].second);
        }
        GamePack q;
        CHECK(Read(plain, q, why) && q.files.size() == 4 && q.TotalFileBytes() == p.TotalFileBytes());

        // Unpack into a fresh folder, then again over it (the cache is replaced).
        const fs::path root = fs::temp_directory_path() / ("okay_browser_game_" + std::to_string((unsigned long long)std::hash<std::string>()(why + __TIME__)));
        fs::remove_all(root);
        const fs::path dir = root / "sites" / "https_example.com" / "games" / "test_1";
        CHECK(UnpackGamePack(p, dir.u8string(), why));
        CHECK(FileText(dir / "game.okayscene") == "scene data");
        CHECK(FileText(dir / "assets" / "sprites" / "hero.png") == game[2].second);
        CHECK(fs::exists(dir / "empty.txt") && fs::file_size(dir / "empty.txt") == 0);
        CHECK(!fs::exists(dir.u8string() + ".part"));
        std::ofstream(dir / "stale.txt") << "old";
        CHECK(UnpackGamePack(p, dir.u8string(), why));
        CHECK(!fs::exists(dir / "stale.txt"));   // replaced, not merged
        CHECK(!UnpackGamePack(p, dir.u8string(), why, false) && !why.empty());
        CHECK(fs::exists(dir / "game.okayscene"));   // a refused unpack leaves the old game as it was
        fs::remove_all(root);
    }

    // ---- truncated ----
    {
        CHECK(Refused(Bytes(), "empty"));
        CHECK(Refused(Bytes(plain.begin(), plain.begin() + 6), "cut inside the magic"));
        CHECK(Refused(Bytes(plain.begin(), plain.begin() + 20), "cut inside the manifest"));
        CHECK(Refused(Bytes(plain.begin(), plain.end() - 3), "cut inside the last file"));
        CHECK(Refused(Bytes(packed.begin(), packed.end() - 10), "gzip cut off"));
        Bytes huge = plain;
        huge[8] = 0xff; huge[9] = 0xff; huge[10] = 0xff; huge[11] = 0x7f;   // a manifest length past the end
        CHECK(Refused(huge, "manifest length past the end"));
        CHECK(Refused(MakePack({}, "{\"files\":[{\"path\":\"/a\",\"offset\":18446744073709551615,\"size\":1}]}"), "offset that overflows"));
        CHECK(Refused(MakePack({{"/a", "abc"}}, "{\"files\":[{\"path\":\"/a\",\"offset\":2,\"size\":9007199254740992}]}"), "size past the end"));
    }

    // ---- oversized ----
    {
        GamePackLimits lim;
        lim.maxPackedBytes = 64;
        CHECK(Refused(plain, "download over the cap", lim));
        lim = GamePackLimits();
        lim.maxUnpackedBytes = 40;
        CHECK(Refused(packed, "unpacked over the cap (gzip stops at the cap)", lim));
        CHECK(Refused(plain, "plain pack over the unpacked cap", lim));
        lim = GamePackLimits();
        lim.maxFiles = 3;
        CHECK(Refused(plain, "too many files", lim));
        lim = GamePackLimits();
        lim.maxManifestBytes = 16;
        CHECK(Refused(plain, "manifest over the cap", lim));
    }

    // ---- names that must not be written ----
    {
        for (const char* bad : {"/../evil.exe", "/a/../../evil", "/a/..", "relative.txt", "/", "//server/share/x", "/C:/Windows/x",
                                "/a\\..\\..\\x", "/con.txt", "/a/nul", "/a:stream", "/dir/", "/x\x01y"}) {
            if (!Refused(MakePack({{bad, "x"}}), bad)) std::cerr << "  (name " << bad << ")\n";
        }
        CHECK(Refused(MakePack({{"/a.txt", "1"}, {"/a.txt", "2"}}), "the same name twice"));
        CHECK(Refused(MakePack({{"/Readme.txt", "1"}, {"/README.TXT", "2"}}), "the same name in another letter case"));
        CHECK(Refused(MakePack({{"/a", "1"}, {"/a/b", "2"}}), "a file and a folder with one name"));
        CHECK(Refused(MakePack({}, "{\"files\":[{\"path\":7,\"offset\":0,\"size\":0}]}"), "path not a string"));
        CHECK(Refused(MakePack({}, "{\"files\":[{\"path\":\"/a\",\"offset\":-1,\"size\":0}]}"), "negative offset"));
        CHECK(Refused(MakePack({}, "{\"files\":[{\"path\":\"/a\",\"offset\":0.5,\"size\":0}]}"), "fractional offset"));
        CHECK(Refused(MakePack({}, "{\"nofiles\":[]}"), "no file list"));
        CHECK(Refused(MakePack({}, "{\"files\":[}"), "broken JSON"));
        // A name with an escape is read as JSON reads it: "\u002e\u002e/x" is "../x".
        CHECK(Refused(MakePack({}, "{\"files\":[{\"path\":\"/\\u002e\\u002e/x\",\"offset\":0,\"size\":0}]}"), "escaped .."));
        GamePack p;
        std::string why;
        CHECK(Read(MakePack({{"/sp ace/\xc3\xa9t\xc3\xa9.txt", "ok"}}), p, why) && p.files.size() == 1);   // spaces and UTF-8 are fine
    }

    // ---- bad gzip ----
    {
        Bytes b = packed;
        b[b.size() - 6] ^= 0x55;   // the CRC-32 no longer matches
        CHECK(Refused(b, "gzip CRC mismatch"));
        b = packed;
        b[2] = 7;                  // not deflate
        CHECK(Refused(b, "gzip with another method"));
        b = packed;
        b.push_back(0);            // bytes after the member
        CHECK(Refused(b, "gzip with extra bytes"));
        CHECK(Refused(Gzip(Bytes{'n', 'o', 't', ' ', 'a', ' ', 'p', 'a', 'c', 'k', '!', '!', '!'}), "gzip of something that is not a pack"));
        b = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff, 0x07, 0, 0, 0, 0, 0, 0, 0, 0, 0};   // a reserved block type
        CHECK(Refused(b, "broken deflate data"));
    }

    // ---- which elements run a game, and from where ----
    {
        const std::string page = "https://games.example.com/play/index.html";
        WebExportConfig none;
        CHECK(GamePackUrl(page, "okay-game", "game.okaypack.gz", none) == "https://games.example.com/play/game.okaypack.gz");
        CHECK(GamePackUrl(page, "okay-game", "/packs/a.okaypack#x", none) == "https://games.example.com/packs/a.okaypack");
        CHECK(GamePackUrl(page, "okay-game", "https://games.example.com:443/b.okaypack", none) == "https://games.example.com:443/b.okaypack");   // the default port is the same origin
        CHECK(GamePackUrl(page, "okay-game", "https://evil.example.net/b.okaypack", none).empty());   // another site
        CHECK(GamePackUrl(page, "okay-game", "http://games.example.com/b.okaypack", none).empty());   // another scheme is another origin
        CHECK(GamePackUrl(page, "okay-game", "file:///etc/passwd", none).empty());
        CHECK(GamePackUrl(page, "okay-game", "javascript:alert(1)", none).empty());
        CHECK(GamePackUrl(page, "okay-game", "", none).empty());
        CHECK(GamePackUrl("file:///home/x/index.html", "okay-game", "game.okaypack", none).empty());
        CHECK(GamePackUrl(page, "canvas", "", none).empty());   // a canvas only with the web export's config
        CHECK(GamePackUrl(page, "video", "game.okaypack", none).empty());

        // The page the web export writes: the config in <head>, a <canvas> in the body.
        const std::string shell =
            "<!DOCTYPE html><html><head><meta charset=utf-8><script>window.OKAY_WEB_CONFIG={\"title\":\"Space \\\"Run\\\" }\","
            "\"gameId\":\"okay_space_run\",\"fit\":\"fill\",\"width\":960,\"height\":600,\"wasm\":\"okay-player.wasm.gz\",\"wasmBytes\":10,"
            "\"packs\":[{\"file\":\"game.okaypack.gz\",\"bytes\":1234}]};</script><title>Space Run</title></head>"
            "<body><canvas id=canvas tabindex=0></canvas><script>var config = window.OKAY_WEB_CONFIG || {};</script></body></html>";
        const HtmlDocument doc = ParseHtml(shell);
        const WebExportConfig cfg = FindWebExportConfig(doc);
        CHECK(cfg.found && cfg.title == "Space \"Run\" }" && cfg.gameId == "okay_space_run");
        CHECK(cfg.packs.size() == 1 && cfg.packs[0] == "game.okaypack.gz");
        CHECK(GamePackUrl(page, "canvas", "", cfg) == "https://games.example.com/play/game.okaypack.gz");
        CHECK(!FindWebExportConfig(ParseHtml("<script>var OKAY_WEB_CONFIG_X = 1;</script><p>hi")).found);
        CHECK(!FindWebExportConfig(ParseHtml("<script>window.OKAY_WEB_CONFIG={broken</script>")).found);
        CHECK(!FindWebExportConfig(ParseHtml("<p>window.OKAY_WEB_CONFIG={\"packs\":[]}</p>")).found);   // only inside <script>
    }

    // ---- per-site folders and the player's command line ----
    {
        CHECK(SiteFolderName("https://Example.com:8443") == "https_example.com_8443");
        CHECK(SiteFolderName("https://games.example.com") == "https_games.example.com");
        CHECK(SiteFolderName("") == "site");
        const std::string longOrigin = "https://" + std::string(200, 'a') + ".com";
        const std::string ln = SiteFolderName(longOrigin);
        CHECK(ln.size() <= 80 && ln != SiteFolderName("https://" + std::string(201, 'a') + ".com"));
        for (const std::string& o : {std::string("https://../.."), std::string("http://x/..\\..")}) {
            const std::string n = SiteFolderName(o);
            CHECK(n.find("..") == std::string::npos || n.find('/') == std::string::npos);
            CHECK(n.find('/') == std::string::npos && n.find('\\') == std::string::npos && n != "." && n != "..");
        }
        const std::string a = GameFolderName("https://g.example.com/a/game.okaypack.gz");
        const std::string b = GameFolderName("https://g.example.com/b/game.okaypack.gz");
        CHECK(a.rfind("game_", 0) == 0 && b.rfind("game_", 0) == 0 && a != b);
        CHECK(GameFolderName("https://g.example.com/x/Space%20Run.okaypack").rfind("space_20run_", 0) == 0);
        CHECK(GameFolderName("https://g.example.com/").rfind("game_", 0) == 0);

        const GameLaunchPlan plan = MakeGameLaunchPlan("/data/browser", "/opt/okay/okay-player", "https://g.example.com/a/game.okaypack.gz");
        CHECK(plan.siteDir == "/data/browser/sites/https_g.example.com/");
        CHECK(plan.gameDir == plan.siteDir + "games/" + a + "/");
        CHECK(plan.saveDir == plan.siteDir + "saves/" + a + "/");
        CHECK(plan.downloadPath == plan.siteDir + "games/" + a + ".download");
        CHECK(plan.argv.size() == 3 && plan.argv[0] == "/opt/okay/okay-player");
        CHECK(plan.argv[1] == "--game-dir=" + plan.gameDir && plan.argv[2] == "--save-dir=" + plan.saveDir);
        const GameLaunchPlan other = MakeGameLaunchPlan("/data/browser/", "p", "https://other.example.org/a/game.okaypack.gz");
        CHECK(other.siteDir != plan.siteDir && other.saveDir.rfind(other.siteDir, 0) == 0);
    }

    TEST_MAIN_RESULT();
}
