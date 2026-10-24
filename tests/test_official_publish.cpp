#include "test_framework.hpp"
#include "OfficialGame.hpp"
#include <filesystem>
#include <fstream>

// Publishing the official game from the launcher (Admin > Official game): the
// folder checks (same rules as scripts/publish-official-game.sh), the next
// version, the manifest it uploads, the bucket listing, and which versions
// "Make live" and "Clean up old versions" may pick.
namespace {
namespace fs = std::filesystem;
using namespace okayofficial;

void Put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

bool Has(const std::vector<std::string>& problems, const std::string& needle) {
    for (const std::string& p : problems) if (p.find(needle) != std::string::npos) return true;
    return false;
}

// A good folder: what File > Build Game makes.
PublishFolder Good() {
    PublishFolder pf;
    pf.title = "Okay Sandbox"; pf.exe = "Okay Sandbox.exe"; pf.exeCandidates = 1; pf.exeIsWindows = true;
    pf.files = {{"Data/game.okayconfig", 40}, {"Data/main.okayscene", 2000}, {"Okay Sandbox.exe", 900000}, {"SDL2.dll", 1500000}};
    return pf;
}
} // namespace

int main() {
    RUN_SUITE("official_publish");

    // ---- Next version ----
    CHECK(SuggestVersion("", "", 2026, 10, 24) == "2026.10.24.1");
    CHECK(SuggestVersion("", "1.4.0", 2026, 10, 24) == "2026.10.24.1");
    CHECK(SuggestVersion("", "2026.10.24.1", 2026, 10, 24) == "2026.10.24.2");
    CHECK(SuggestVersion("", "2026.10.24.9", 2026, 10, 24) == "2026.10.24.10");
    CHECK(SuggestVersion("", "2026.10.23.4", 2026, 10, 24) == "2026.10.24.1");
    CHECK(SuggestVersion("", "2026.1.5.2", 2026, 1, 5) == "2026.01.05.1");   // only the script's zero-padded form counts
    CHECK(SuggestVersion("", "2026.01.05.2", 2026, 1, 5) == "2026.01.05.3");
    CHECK(SuggestVersion(" 1.3.0\r", "2026.10.24.1", 2026, 10, 24) == "1.3.0");   // the folder's VERSION.txt wins
    CHECK(SuggestVersion("", "2026.10.24.x", 2026, 10, 24) == "2026.10.24.1");

    // ---- Folder checks: each refusal ----
    CHECK(CheckPublish(Good(), "1.0.0", "", false).empty());
    CHECK(CheckPublish(Good(), "1.0.1", "1.0.0", false).empty());
    {
        PublishFolder pf; CHECK(Has(CheckPublish(pf, "1.0", "", false), "empty"));
    }
    {
        PublishFolder pf = Good(); pf.exe.clear(); pf.exeCandidates = 0;
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "No game .exe"));
    }
    {
        PublishFolder pf = Good(); pf.exe.clear(); pf.exeCandidates = 2;
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "Found 2 game .exe"));
    }
    {
        PublishFolder pf = Good(); pf.exeIsWindows = false;
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "isn't a Windows program"));
    }
    {
        PublishFolder pf = Good(); pf.files.push_back({"Data/huge.pak", kMaxUploadBytes + 1});
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "over the 50 MB file limit"));
        pf.files.back().size = kMaxUploadBytes;   // exactly the limit is fine
        CHECK(CheckPublish(pf, "1.0", "", false).empty());
    }
    {
        PublishFolder pf = Good(); pf.files.push_back({"Data/a:b.png", 1});
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "rename it"));
        pf.files.back().path = "Data/CON.png";
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "reserved device name"));
        pf.files.back().path = "Data/trailing. ";
        CHECK(!CheckPublish(pf, "1.0", "", false).empty());
    }
    {
        PublishFolder pf = Good(); pf.files.push_back({"sdl2.DLL", 1});
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "only in case"));
    }
    {
        PublishFolder pf = Good(); pf.files.push_back({"data", 1});   // a file "data" and a folder "Data"
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "inside a file"));
    }
    {
        PublishFolder pf = Good(); pf.files.push_back({"Manifest.json", 10});
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "manifest.json"));
        pf.files.back().path = "Data/manifest.json";   // only the top-level one clashes
        CHECK(CheckPublish(pf, "1.0", "", false).empty());
    }
    {
        PublishFolder pf = Good(); for (auto& f : pf.files) f.size = 0;
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "all empty"));
    }
    {
        PublishFolder pf = Good();
        for (int i = 0; i < 210; ++i) pf.files.push_back({"Data/big" + std::to_string(i) + ".pak", kMaxUploadBytes});
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "over 10 GB"));
    }
    {
        PublishFolder pf = Good(); pf.title.clear(); pf.exe.clear(); pf.exeCandidates = 0;
        CHECK(Has(CheckPublish(pf, "1.0", "", false), "no name"));
    }
    CHECK(Has(CheckPublish(Good(), "v1.0", "", false), "Bad version"));
    CHECK(Has(CheckPublish(Good(), "1", "", false), "Bad version"));
    CHECK(Has(CheckPublish(Good(), "1.0.0", "1.0.0", false), "isn't newer"));
    CHECK(Has(CheckPublish(Good(), "0.9", "1.0.0", false), "isn't newer"));
    CHECK(Has(CheckPublish(Good(), "1.9", "1.10", false), "isn't newer"));
    CHECK(CheckPublish(Good(), "1.0.0", "1.0.0", true).empty());   // "Allow same or older" (FORCE=1)
    CHECK(CheckPublish(Good(), "0.9", "1.0.0", true).empty());

    // ---- Reading a real folder ----
    {
        const fs::path dir = fs::temp_directory_path() / "okay_official_publish_scan";
        fs::remove_all(dir);
        Put(dir / "Okay Sandbox.exe", std::string("MZ\x90\0", 4) + "rest");
        Put(dir / "Okay Sandbox-Server.exe", "MZserver");
        Put(dir / "SDL2.dll", "dll");
        Put(dir / "cover.png", "png");
        Put(dir / "VERSION.txt", "1.4.2\nignored\n");
        Put(dir / "Data/game.okayconfig", "width=1280\ntitle=Okay Sandbox\r\nheight=720\n");
        Put(dir / "Data/main.okayscene", "scene");
        Put(dir / ".git/config", "hidden");
        Put(dir / "Data/.DS_Store", "hidden");
        PublishFolder pf;
        std::string err;
        CHECK(ScanFolder(dir, pf, &err));
        CHECK(pf.title == "Okay Sandbox" && pf.name() == "Okay Sandbox");
        CHECK(pf.exe == "Okay Sandbox.exe" && pf.exeIsWindows);
        CHECK(pf.image == "cover.png");
        CHECK(pf.folderVersion == "1.4.2");
        CHECK(pf.files.size() == 7);   // hidden files left out
        CHECK(pf.files.front().path == "Data/game.okayconfig" && pf.files.back().path == "cover.png");   // bytewise order
        CHECK(pf.total() == 8 + 8 + 3 + 3 + 14 + 42 + 5);
        CHECK(CheckPublish(pf, SuggestVersion(pf.folderVersion, "", 2026, 10, 24), "", false).empty());
        // No title: the only non-server exe at the top.
        fs::remove(dir / "Data/game.okayconfig");
        CHECK(ScanFolder(dir, pf) && pf.exe == "Okay Sandbox.exe" && pf.name() == "Okay Sandbox");
        // Two game exes: say so.
        Put(dir / "Other.exe", "MZ");
        CHECK(ScanFolder(dir, pf) && pf.exe.empty() && pf.exeCandidates == 2);
        fs::remove(dir / "Other.exe");
        // A Linux build: not a Windows exe.
        Put(dir / "Okay Sandbox.exe", "\x7f" "ELF");
        CHECK(ScanFolder(dir, pf) && !pf.exeIsWindows && Has(CheckPublish(pf, "1.0", "", false), "Windows"));
        fs::remove_all(dir);
        CHECK(!ScanFolder(dir, pf, &err) && !err.empty());
    }

    // ---- The manifest it uploads ----
    {
        // sha256 of known bytes (FIPS 180-2 test vectors).
        CHECK(okay::account::detail::Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(okay::account::detail::Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        const fs::path f = fs::temp_directory_path() / "okay_official_publish_abc.bin";
        Put(f, "abc");
        CHECK(FileSha256(f) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        fs::remove(f);

        CHECK(PublishNotes("  - New map\r\n- Faster\r\n ", "G", "1.0") == "- New map\n- Faster");
        CHECK(PublishNotes(" \n ", "Okay Sandbox", "1.2") == "- Okay Sandbox v1.2");

        std::vector<File> files = {
            {"Okay Sandbox.exe", 3, okay::account::detail::Sha256Hex("MZx")},
            {"Data/My Level #1 (caf\xC3\xA9).okayscene", 3, okay::account::detail::Sha256Hex("abc")},
            {"cover.png", 0, okay::account::detail::Sha256Hex("")}};
        const Manifest m = MakeManifest("Okay \"Sandbox\"", "2026.10.24.1", "Okay Sandbox.exe",
                                        PublishNotes("- New\n- \\o/ caf\xC3\xA9", "x", "y"), "cover.png", files);
        CHECK(m.size == 6);
        Manifest back;
        std::string err;
        CHECK(ParseManifest(ToJson(m), back, &err));
        CHECK(back.name == m.name && back.version == "2026.10.24.1" && back.exe == "Okay Sandbox.exe");
        CHECK(back.notes == "- New\n- \\o/ caf\xC3\xA9" && back.image == "cover.png" && back.size == 6);
        CHECK(back.files.size() == 3 && back.files[1].path == "Data/My Level #1 (caf\xC3\xA9).okayscene" &&
              back.files[1].sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        // ...and a player's launcher would install it: a fresh diff fetches every file.
        CHECK(Diff(nullptr, back).download.size() == 3);
        // No cover: the field is left out, still parses.
        Manifest noImg = m; noImg.image.clear();
        CHECK(ParseManifest(ToJson(noImg), back) && back.image.empty());
    }
    CHECK(std::string(ContentType("cover.PNG")) == "image/png" && std::string(ContentType("a.jpeg")) == "image/jpeg");
    CHECK(std::string(ContentType("Data/game.okayconfig")).rfind("text/plain", 0) == 0);
    CHECK(std::string(ContentType("SDL2.dll")) == "application/octet-stream");

    // ---- The bucket listing ----
    {
        const std::string list =
            "[{\"name\":\"NOTES.txt\",\"id\":\"a1\",\"metadata\":{\"size\":120,\"mimetype\":\"text/plain\"}},"
            " {\"name\":\"VERSION.txt\",\"id\":\"a2\",\"metadata\":{\"size\":13}},"
            " {\"name\":\"v1.9.0\",\"id\":null,\"metadata\":null},"
            " {\"name\":\"v1.10.0\",\"id\":null,\"metadata\":null},"
            " {\"name\":\"v2026.10.24.1\",\"id\":null},"
            " {\"name\":\"vbad\",\"id\":null},"
            " {\"name\":\"v1.2.0\",\"id\":\"file-not-folder\"},"
            " {\"name\":\"stray\",\"id\":null}]";
        std::vector<StorageEntry> e;
        CHECK(ParseStorageList(list, e) && e.size() == 8);
        CHECK(!e[0].folder && e[0].size == 120 && e[2].folder);
        const std::vector<std::string> v = VersionFolders(e);
        CHECK(v.size() == 3 && v[0] == "2026.10.24.1" && v[1] == "1.10.0" && v[2] == "1.9.0");
        CHECK(ParseStorageList("[]", e) && e.empty());
        CHECK(!ParseStorageList("{\"error\":\"nope\"}", e));
        CHECK(!ParseStorageList("not json", e));
    }

    // ---- Make live (rollback) / clean up ----
    {
        const std::vector<std::string> v = {"1.3.0", "1.2.0", "1.1.0", "1.0.0"};   // newest first
        auto ok = [](const std::string& x) { return x != "1.1.0"; };              // 1.1.0's upload never finished
        CHECK(RollbackTarget(v, "1.3.0", ok) == "1.2.0");
        CHECK(RollbackTarget(v, "1.2.0", ok) == "1.0.0");    // skips the incomplete 1.1.0
        CHECK(RollbackTarget(v, "1.0.0", ok).empty());       // nothing older
        CHECK(RollbackTarget(v, "1.2.0", nullptr) == "1.1.0");
        CHECK(MakeLiveProblem("1.2.0", "1.3.0", v, true).empty());
        CHECK(MakeLiveProblem("1.3.0", "1.3.0", v, true).find("already live") != std::string::npos);
        CHECK(MakeLiveProblem("1.1.0", "1.3.0", v, false).find("no complete file list") != std::string::npos);
        CHECK(MakeLiveProblem("0.5.0", "1.3.0", v, true).find("isn't in the bucket") != std::string::npos);
        CHECK(MakeLiveProblem("1.3.0", "1.2.0", v, true).empty());   // "forward" to a newer one that was rolled back from

        CHECK((CleanupVersions(v, "1.3.0", 3) == std::vector<std::string>{"1.0.0"}));
        CHECK((CleanupVersions(v, "1.3.0", 2) == std::vector<std::string>{"1.1.0", "1.0.0"}));
        CHECK((CleanupVersions(v, "1.0.0", 2) == std::vector<std::string>{"1.2.0", "1.1.0"}));   // the live one is never deleted (and counts)
        CHECK(CleanupVersions(v, "1.3.0", 4).empty() && CleanupVersions(v, "1.3.0", 10).empty());
        CHECK(CleanupVersions(v, "1.3.0", 0).size() == 3);   // keeps at least one
        // Incomplete uploads don't use up "keep" (rollback targets survive); an
        // incomplete one newer than live may be uploading right now, so it stays.
        const std::vector<std::string> w = {"5.0", "4.0", "3.5", "3.0", "2.0", "1.0"};
        auto complete = [](const std::string& x) { return x != "5.0" && x != "3.5"; };
        CHECK((CleanupVersions(w, "4.0", 3, complete) == std::vector<std::string>{"3.5", "1.0"}));
        CHECK((CleanupVersions(w, "1.0", 1, complete) == std::vector<std::string>{"4.0", "3.0", "2.0"}));
        CHECK((CleanupVersions(w, "", 2, complete) == std::vector<std::string>{"2.0", "1.0"}));
    }

    // ---- "Run the SQL" detection ----
    CHECK(StorageDenied(403, ""));
    CHECK(StorageDenied(400, "{\"statusCode\":\"403\",\"error\":\"Unauthorized\",\"message\":\"new row violates row-level security policy\"}"));
    CHECK(!StorageDenied(400, "{\"statusCode\":\"413\",\"error\":\"Payload too large\"}"));
    CHECK(!StorageDenied(401, "") && !StorageDenied(200, "") && !StorageDenied(500, "row-level security"));

    TEST_MAIN_RESULT();
}
