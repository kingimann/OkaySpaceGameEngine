// The asset type table (AssetTypeFromExtension in engine/src/Core/ImportSettings.cpp) decides which files the asset
// database knows. A file type missing from it counts as "Other": references to it are never found, a rename leaves
// stale paths behind, and Find References / is:unused are wrong. This test keeps the table complete:
//  1. It reads every C++ source of the engine, editor and launcher, collects each ".okay<x>" that appears inside a
//     string literal, and checks each one is either in the table or in the explicit NonAssetExtensions() list.
//     A new file type that is in neither fails here, with the file and line where it was first seen.
//  2. For two of the types added to the table (.okayui UI layouts and .okayac anti-cheat rules) it checks the
//     Project panel finds who uses them, and that renaming one rewrites the paths that point at it.
// Written for this project from the engine's own API; no outside code.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "../editor/ProjectBrowser.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#ifndef OKAY_SOURCE_DIR
#error "OKAY_SOURCE_DIR must be set (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;
using namespace okay;

namespace {

bool IsExtChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

// Every ".okay<x>" inside a "string literal" of `line` (lower-cased), added to `found` with where it was seen.
// Character literals ('"') are skipped so they do not open a string; a string runs to its closing quote or the end
// of the line.
void ScanLine(const std::string& rawLine, const std::string& where, std::map<std::string, std::string>& found) {
    std::string line = rawLine;
    for (char& c : line) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '\'') {                               // a char literal: '"', '\'', 'a'
            std::size_t j = i + 1;
            if (j < line.size() && line[j] == '\\') j += 2; else j += 1;
            if (j < line.size() && line[j] == '\'') { i = j + 1; continue; }
            ++i;                                       // an apostrophe in a comment ("don't"): just step over it
            continue;
        }
        if (c != '"') { ++i; continue; }
        std::string lit;
        std::size_t j = i + 1;
        while (j < line.size() && line[j] != '"') {
            if (line[j] == '\\' && j + 1 < line.size()) {   // "\n", "\t": a control char, so ".okayscene\n" ends at the 'e'
                const char x = line[j + 1];
                lit += (x == '"' || x == '\\' || x == '\'') ? x : '\x01';
                j += 2;
                continue;
            }
            lit += line[j++];
        }
        for (std::size_t p = lit.find(".okay"); p != std::string::npos; p = lit.find(".okay", p + 1)) {
            std::size_t e = p + 5;
            while (e < lit.size() && IsExtChar(lit[e])) ++e;
            const std::string ext = lit.substr(p + 1, e - p - 1);
            if (!found.count(ext)) found[ext] = where;
        }
        i = j + 1;
    }
}

std::map<std::string, std::string> ScanSources() {
    std::map<std::string, std::string> found;   // extension -> first "file:line"
    const fs::path root = fs::u8path(OKAY_SOURCE_DIR);
    for (const char* dir : {"engine", "editor", "launcher"}) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root / dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string x = it->path().extension().string();
            if (x != ".cpp" && x != ".hpp" && x != ".h" && x != ".inc" && x != ".c") continue;
            std::ifstream in(it->path(), std::ios::binary);
            std::string line;
            int n = 0;
            const std::string rel = fs::relative(it->path(), root, ec).generic_string();
            while (std::getline(in, line)) ScanLine(line, rel + ":" + std::to_string(++n), found);
        }
    }
    return found;
}

void TestTableCoversSources() {
    RUN_SUITE("every .okay<x> the sources use is in the asset table or the not-assets list");
    const auto found = ScanSources();
    std::cout << "  found " << found.size() << " .okay extensions in string literals:";
    for (const auto& kv : found) std::cout << ' ' << kv.first;
    std::cout << "\n";
    // The scanner itself works: it sees types we know are used.
    CHECK(found.size() >= 20);
    for (const char* known : {"okayscene", "okayprefab", "okaymeta", "okayac", "okayui", "okayprefs"})
        CHECK(found.count(known) == 1);

    int missing = 0;
    for (const auto& kv : found) {
        const bool asset = AssetTypeFromExtension(kv.first) != AssetType::Other;
        const bool notAsset = IsNonAssetExtension(kv.first);
        if (!asset && !notAsset) {
            ++missing;
            std::cerr << "  MISSING: ." << kv.first << " (first seen at " << kv.second
                      << ") is neither in the asset table nor in NonAssetExtensions()\n";
        }
        CHECK(asset || notAsset);
    }
    std::cout << "  missing from both lists: " << missing << "\n";

    // The two lists never overlap, and every not-asset entry is lower case with no dot.
    for (const std::string& e : NonAssetExtensions()) {
        CHECK(AssetTypeFromExtension(e) == AssetType::Other);
        CHECK(!e.empty() && e[0] != '.');
        CHECK(std::none_of(e.begin(), e.end(), [](char c) { return std::isupper(static_cast<unsigned char>(c)); }));
        CHECK(IsNonAssetExtension("." + e));
    }
    CHECK(!IsNonAssetExtension("okayscene"));
    CHECK(IsNonAssetExtension(".OkayPrefs"));
}

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_aet_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path / "Assets", ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string assets() const { return (path / "Assets").generic_string(); }
    std::string at(const std::string& rel) const { return (path / "Assets" / fs::u8path(rel)).generic_u8string(); }
};

void WriteFile(const std::string& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(fs::u8path(p).parent_path(), ec);
    std::ofstream(fs::u8path(p), std::ios::binary | std::ios::trunc) << s;
}
std::string ReadFile(const std::string& p) {
    std::ifstream in(fs::u8path(p), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

void TestNewTypesUsedAndRenamed() {
    RUN_SUITE(".okayui and .okayac: who uses them is found, and a rename rewrites the paths");
    CHECK(AssetTypeFromExtension("okayui") == AssetType::Data);
    CHECK(AssetTypeFromExtension("okayac") == AssetType::Data);
    TempDir td;
    const std::string ui = td.at("ui/hud.okayui");
    const std::string rules = td.at("net/rules.okayac");
    const std::string panel = td.at("art/panel.png");
    const std::string script = td.at("scripts/game.okay");
    WriteFile(panel, "not really a png");
    WriteFile(ui, "image texture=Assets/art/panel.png nineslice=1\ntext \"Score\" pos=1,1\n");
    WriteFile(rules, "# OkaySpace anti-cheat settings (.okayac)\nmaxSpeed 12\n");
    WriteFile(script,
        "ui_load(\"Assets/ui/hud.okayui\")\n"
        "net_anticheat_load(\"Assets/net/rules.okayac\")\n"
        "print(\"ready\")\n");

    okaypb::Browser b(td.assets());
    CHECK(b.Sync());
    CHECK(b.UsersOf(ui) == 1);                         // the script uses the UI layout
    CHECK(b.UsersOf(rules) == 1);                      // the script uses the anti-cheat rules
    CHECK(b.UsersOf(panel) == 1);                      // the UI layout uses the picture

    auto r = b.Rename(ui, "main_hud");
    CHECK(r.ok);
    CHECK(r.referencesRewritten == 1);
    std::string code = ReadFile(script);
    CHECK(Has(code, "ui_load(\"Assets/ui/main_hud.okayui\")"));
    CHECK(!Has(code, "ui/hud.okayui"));
    CHECK(b.UsersOf(td.at("ui/main_hud.okayui")) == 1);

    auto a = b.Rename(rules, "strict");
    CHECK(a.ok);
    CHECK(a.referencesRewritten == 1);
    code = ReadFile(script);
    CHECK(Has(code, "net_anticheat_load(\"Assets/net/strict.okayac\")"));
    CHECK(!Has(code, "net/rules.okayac"));
    CHECK(Has(code, "print(\"ready\")"));              // the rest of the script is left alone
    CHECK(b.UsersOf(td.at("net/strict.okayac")) == 1);

    auto p = b.Rename(panel, "frame");                 // a path inside the UI layout is rewritten too
    CHECK(p.ok);
    CHECK(p.referencesRewritten == 1);
    CHECK(Has(ReadFile(td.at("ui/main_hud.okayui")), "texture=Assets/art/frame.png"));
}

} // namespace

int main() {
    TestTableCoversSources();
    TestNewTypesUsedAndRenamed();
    TEST_MAIN_RESULT();
}
