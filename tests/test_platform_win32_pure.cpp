// The pure parts of the own Win32 clipboard / drag and drop / IME code (Platform/Win32Exchange.hpp): byte layouts and
// text conversions, run natively on any host. The Windows calls around them are in test_platform_win32.cpp (Wine).
#include "test_framework.hpp"
#include "Platform/Win32Exchange.hpp"

using namespace okay::platform;
using namespace okay::platform::win32;

namespace {

void TestDropFiles() {
    RUN_SUITE("CF_HDROP blob");
    const std::vector<std::string> paths = {"C:/Users/a b/\xC3\xA9.txt", "D:\\x\\y.png", "", "/home/u/z"};
    const std::vector<uint8_t> b = BuildDropFiles(paths, 12, 34);
    CHECK(b.size() > kDropFilesHeader);
    CHECK(b[0] == 20 && b[1] == 0 && b[16] == 1);                         // pFiles = 20, fWide = 1
    CHECK(b[b.size() - 1] == 0 && b[b.size() - 2] == 0 && b[b.size() - 3] == 0 && b[b.size() - 4] == 0);   // last NUL + list end
    int px = 0, py = 0;
    const auto back = ParseDropFiles(b.data(), b.size(), &px, &py);
    CHECK(px == 12 && py == 34);
    CHECK(back.size() == 3);                                              // the empty path was skipped
    if (back.size() == 3) CHECK(back[0] == "C:\\Users\\a b\\\xC3\xA9.txt" && back[1] == "D:\\x\\y.png" && back[2] == "\\home\\u\\z");
    CHECK(ParseDropFiles(BuildDropFiles({}).data(), BuildDropFiles({}).size()).empty());
    // Hostile blobs never read out of range.
    CHECK(ParseDropFiles(nullptr, 100).empty());
    CHECK(ParseDropFiles(b.data(), 10).empty());                          // shorter than the header
    std::vector<uint8_t> bad = b; bad[0] = 0xFF; bad[1] = 0xFF;           // offset far past the end
    CHECK(ParseDropFiles(bad.data(), bad.size()).empty());
    bad = b; bad[0] = 4;                                                  // offset inside the header
    CHECK(ParseDropFiles(bad.data(), bad.size()).empty());
    bad = b; bad.resize(bad.size() - 6);                                  // cut off in the middle of the last path
    const auto cut = ParseDropFiles(bad.data(), bad.size());
    CHECK(cut.size() == 2);
    bad = b; bad.resize(21);                                              // one odd byte after the header
    CHECK(ParseDropFiles(bad.data(), bad.size()).empty());
    // An ANSI list (fWide = 0).
    std::vector<uint8_t> ansi(20, 0); ansi[0] = 20;
    for (char c : std::string("C:\\a.txt")) ansi.push_back((uint8_t)c);
    ansi.push_back(0); ansi.push_back(0);
    const auto an = ParseDropFiles(ansi.data(), ansi.size());
    CHECK(an.size() == 1 && an[0] == "C:\\a.txt");
    // Many paths are capped.
    std::vector<std::string> many(kMaxDropPaths + 50, "C:/f");
    const auto mb = BuildDropFiles(many);
    CHECK(ParseDropFiles(mb.data(), mb.size()).size() == kMaxDropPaths);
}

void TestUri() {
    RUN_SUITE("path <-> file URI");
    CHECK(PathToFileUri("C:\\a b\\c.txt") == "file:///C:/a%20b/c.txt");
    CHECK(PathToFileUri("/home/a") == "file:///home/a");
    CHECK(PathToFileUri("/tmp/\xC3\xA9#?%") == "file:///tmp/%C3%A9%23%3F%25");
    // Round trip through the parser of Exchange.hpp.
    for (const std::string p : {"C:/x y/z.txt", "/home/u/\xC3\xA9\xE4\xBD\xA0 #1?.png", "D:/100%/a.b"}) {
        CHECK(FileUriToPath(PathToFileUri(p)) == p);
    }
    const auto blob = BuildDropFiles({"C:\\one.txt", "C:\\two three.txt"});
    const std::string uris = DropFilesToUriList(blob.data(), blob.size());
    CHECK(uris == "file:///C:/one.txt\r\nfile:///C:/two%20three.txt\r\n");
    const auto round = UriListToDropFiles(uris);
    const auto paths = ParseDropFiles(round.data(), round.size());
    CHECK(paths.size() == 2 && paths[0] == "C:\\one.txt" && paths[1] == "C:\\two three.txt");
    CHECK(UriListToDropFiles("http://example.com/\r\n").empty());
}

void TestIme() {
    RUN_SUITE("IME composition");
    const std::u16string s = u"\u4F60\u597D";
    Event e = MakeCompositionEvent(s, 1, 77, 5);
    CHECK(e.type == EventType::TextEditing && e.windowId == 77 && e.text == "\xE4\xBD\xA0\xE5\xA5\xBD" && e.editStart == 1 && e.editLength == 0 && e.timestampMs == 5);
    // A surrogate pair counts as ONE character in the caret position.
    const std::u16string em = u"a\xD83D\xDE00" u"b";
    CHECK(ImeCursorToChars(em, 0) == 0 && ImeCursorToChars(em, 1) == 1 && ImeCursorToChars(em, 3) == 2 && ImeCursorToChars(em, 4) == 3);
    CHECK(ImeCursorToChars(em, 99) == 3 && ImeCursorToChars(em, -5) == 0);
    CHECK(ImeCursorToChars(u"", 3) == 0);
    // A lone surrogate is one character and becomes U+FFFD in the text.
    const std::u16string lone = u"\xD83Dx";
    e = MakeCompositionEvent(lone, 2, 1);
    CHECK(e.text == "\xEF\xBF\xBD" "x" && e.editStart == 2);
    e = MakeCompositionEvent(u"", 0, 1);
    CHECK(e.text.empty());                                               // composition ended
}

} // namespace

int main() {
    TestDropFiles();
    TestUri();
    TestIme();
    TEST_MAIN_RESULT();
}
