// SEC1 item 2: the one path validator every script-facing file API shares (engine/include/okay/Core/PathSafety.hpp).
// A table of hostile names with the expected verdict in each mode (Portable = what scripts use, Windows rules, POSIX
// rules), a symlink-escape check on a real temp folder, and a check that every script-facing API refuses them.
#include "test_framework.hpp"
#include "okay/Core/PathSafety.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace ps = okay::pathsafety;
namespace fs = std::filesystem;

struct Row { std::string name; bool portable, windows, posix; const char* what; };

int main() {
    RUN_SUITE("path_safety");
    const std::string longName(ps::kMaxPathBytes + 1, 'a');
    const std::string longParts = [] { std::string s; while (s.size() <= ps::kMaxPathBytes) s += "dir/"; return s + "f"; }();
    const std::vector<Row> table = {
        // name                              Portable Windows Posix
        {"save.okaysave",                    true,  true,  true,  "plain name"},
        {"slots/one.okaysave",               true,  true,  true,  "subfolder"},
        {"slots\\one.okaysave",              true,  true,  true,  "backslash separator (a literal char on POSIX)"},
        {"./local.txt",                      true,  true,  true,  "current folder"},
        {"a..b.txt",                         true,  true,  true,  "dots inside a name"},
        {"..hidden",                         true,  true,  true,  "leading dots that are not a '..' part"},
        {"dir/.hidden",                      true,  true,  true,  "dot file"},
        {"my save (1).txt",                  true,  true,  true,  "spaces and brackets inside"},
        {"sp\xC3\xA4ter.txt",                true,  true,  true,  "non-ASCII"},
        {"console.txt",                      true,  true,  true,  "starts like CON but is not CON"},
        {"com10",                            true,  true,  true,  "COM10 is not a device"},
        {"nullable",                         true,  true,  true,  "starts like NUL"},
        {"",                                 false, false, false, "empty"},
        {"..",                               false, false, false, "parent"},
        {"../x",                             false, false, false, "parent then name"},
        {"a/../../x",                        false, false, false, "climbs out"},
        {"a/b/..",                           false, false, false, "trailing parent"},
        {"a\\..\\x",                         false, false, true,  "backslash parent (POSIX: a literal name)"},
        {"..\\x",                            false, false, true,  "backslash parent at the start"},
        {"/etc/passwd",                      false, false, false, "absolute POSIX"},
        {"/",                                false, false, false, "root"},
        {"\\Windows\\win.ini",               false, false, true,  "rooted Windows (POSIX: literal)"},
        {"\\\\server\\share\\x",             false, false, true,  "UNC"},
        {"//server/share/x",                 false, false, false, "UNC with slashes"},
        {"\\\\?\\C:\\x",                     false, false, true,  "extended-length path"},
        {"\\\\.\\COM1",                      false, false, true,  "device namespace"},
        {"C:\\Windows\\x",                   false, false, true,  "drive letter"},
        {"C:x",                              false, false, true,  "drive-relative"},
        {"c:/x",                             false, false, true,  "drive with slash"},
        {"file.txt:hidden",                  false, false, true,  "NTFS alternate data stream"},
        {"file.txt::$DATA",                  false, false, true,  "ADS data stream name"},
        {"dir/file:stream:$DATA",            false, false, true,  "ADS inside a folder"},
        {"CON",                              false, false, true,  "device CON"},
        {"con",                              false, false, true,  "device con, lower case"},
        {"NUL",                              false, false, true,  "device NUL"},
        {"nul.txt",                          false, false, true,  "device with an extension"},
        {"dir/aux.log",                      false, false, true,  "device inside a folder"},
        {"PRN",                              false, false, true,  "device PRN"},
        {"COM1",                             false, false, true,  "device COM1"},
        {"lpt9.dat",                         false, false, true,  "device LPT9"},
        {"CONIN$",                           false, false, true,  "console input"},
        {"CONOUT$",                          false, false, true,  "console output"},
        {"nul .txt",                         false, false, true,  "device with a space before the dot"},
        {"x.",                               false, false, true,  "trailing dot (Windows strips it)"},
        {"x ",                               false, false, true,  "trailing space (Windows strips it)"},
        {"...",                              false, false, true,  "dots only: Windows reads it as '..'"},
        {"dir./file",                        false, false, true,  "trailing dot on a folder"},
        {"dir /file",                        false, false, true,  "trailing space on a folder"},
        {"a<b",                              false, false, true,  "character Windows cannot store"},
        {"a|b",                              false, false, true,  "pipe"},
        {"what?.txt",                        false, false, true,  "question mark"},
        {"star*.txt",                        false, false, true,  "wildcard"},
        {"quo\"te",                          false, false, true,  "double quote"},
        {std::string("ok\0../x", 7),         false, false, false, "NUL hides what follows"},
        {std::string("a\0b", 3),             false, false, false, "embedded NUL"},
        {"line\nbreak",                      false, false, false, "newline"},
        {"tab\tname",                        false, false, false, "tab"},
        {"bell\x07",                         false, false, false, "control character"},
        {"del\x7f",                          false, false, false, "DEL"},
        {longName,                           false, false, false, "over-long name"},
        {longParts,                          false, false, false, "over-long path of short parts"},
        {std::string(ps::kMaxPathBytes, 'a'),true,  true,  true,  "exactly at the limit"},
    };
    for (const auto& r : table) {
        const bool p = ps::IsSafeRelative(r.name), w = ps::IsSafeRelative(r.name, ps::Mode::Windows), x = ps::IsSafeRelative(r.name, ps::Mode::Posix);
        if (p != r.portable || w != r.windows || x != r.posix)
            std::cerr << "  row \"" << r.what << "\": portable=" << p << " windows=" << w << " posix=" << x << "\n";
        CHECK(p == r.portable);
        CHECK(w == r.windows);
        CHECK(x == r.posix);
        CHECK(ps::IsSafeRelative(r.name) == (ps::Check(r.name) == ps::Why::Ok));
    }
    CHECK(ps::Check("../x") == ps::Why::ParentRef);
    CHECK(ps::Check("/x") == ps::Why::Absolute);
    CHECK(ps::Check("a:b") == ps::Why::DriveOrStream);
    CHECK(ps::Check("nul") == ps::Why::DeviceName);
    CHECK(ps::Check("x.") == ps::Why::TrailingDotOrSpace);
    CHECK(std::string(ps::Describe(ps::Why::Nul)).find("NUL") != std::string::npos);

    // ---- an existing link that points out of the folder ----
    std::mt19937 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("okay_pathsafety_" + std::to_string(rng()));
    fs::create_directories(root / "game" / "sub");
    fs::create_directories(root / "outside");
    std::ofstream(root / "outside" / "secret.txt") << "secret";
    std::ofstream(root / "game" / "ok.txt") << "ok";
    std::error_code ec;
    fs::create_directory_symlink(root / "outside", root / "game" / "link", ec);
    const bool haveLinks = !ec;
    fs::path got;
    std::string why;
    CHECK(ps::StaysInside(root / "game", "ok.txt", &got, &why));
    CHECK(got.filename() == "ok.txt");
    CHECK(ps::StaysInside(root / "game", "sub/new.txt"));            // does not exist yet: fine
    CHECK(ps::StaysInside(root / "game", "nothing/at/all.txt"));
    CHECK(!ps::StaysInside(root / "game", "../outside/secret.txt", nullptr, &why) && !why.empty());
    CHECK(!ps::StaysInside(root / "game", "/etc/passwd"));
    CHECK(!ps::StaysInside(root / "game", "sub/.."));                // '..' by name
    CHECK(!ps::StaysInside(root / "game", "."));                     // the folder itself
    if (haveLinks) {
        CHECK(!ps::StaysInside(root / "game", "link/secret.txt", nullptr, &why));
        CHECK(why.find("outside") != std::string::npos);
        CHECK(!ps::StaysInside(root / "game", "link/new.txt"));      // a write through the link
        // a link to a file
        fs::create_symlink(root / "outside" / "secret.txt", root / "game" / "flink.txt", ec);
        if (!ec) CHECK(!ps::StaysInside(root / "game", "flink.txt"));
        // a link that stays inside is fine
        fs::create_symlink(root / "game" / "sub", root / "game" / "inlink", ec);
        if (!ec) CHECK(ps::StaysInside(root / "game", "inlink/x.txt"));
        // a link loop is refused, not followed for ever
        fs::create_symlink(root / "game" / "loopb", root / "game" / "loopa", ec);
        fs::create_symlink(root / "game" / "loopa", root / "game" / "loopb", ec);
        CHECK(!ps::StaysInside(root / "game", "loopa/x"));
    } else std::cout << "  (symbolic links not available here: link cases skipped)\n";
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
