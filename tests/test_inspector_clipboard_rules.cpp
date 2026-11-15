// What Paste Value does with the text on the system clipboard, the engine-free part (editor/InspectorClipboardRules.hpp): the clipboard
// is read once and only up to a cap, text that is empty / too long / on several lines is refused before anything parses it, the
// preview is short and safe to show, and a file path has to stay inside the project.
// What a value of each kind must look like (nan, ranges ...) is checked with the property registry in tests/test_inspector_menu_actions.cpp.
#include "test_framework.hpp"
#include "InspectorClipboardRules.hpp"
#include <string>
#include <vector>

using namespace okay::inspectorctx;

namespace {

bool Has(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

void TestReadClipboard() {
    RUN_SUITE("the clipboard is read up to a cap");
    CHECK(!ReadClipboard(nullptr).present);
    CHECK(!ReadClipboard("").present);
    const ClipboardText small = ReadClipboard("1 2 3");
    CHECK(small.present && !small.tooLong && small.text == "1 2 3");
    // Exactly the cap is fine; one more character is too long, and only the cap is kept.
    const std::string exact(kMaxClipboardChars, 'a');
    const ClipboardText atCap = ReadClipboard(exact.c_str());
    CHECK(atCap.present && !atCap.tooLong && atCap.text.size() == kMaxClipboardChars);
    const std::string one(kMaxClipboardChars + 1, 'a');
    const ClipboardText over = ReadClipboard(one.c_str());
    CHECK(over.present && over.tooLong && over.text.size() == kMaxClipboardChars);
    // A whole file on the clipboard (8 MB): the copy is the cap, not the file.
    const std::string huge(8u * 1024u * 1024u, 'x');
    const ClipboardText file = ReadClipboard(huge.c_str());
    CHECK(file.tooLong && file.text.size() == kMaxClipboardChars);
    CHECK(kMaxClipboardChars == 64u * 1024u);
}

void TestGate() {
    RUN_SUITE("the first look at the clipboard text");
    ClipboardText none;
    GateResult g = GateClipboard(none, false);
    CHECK(g.gate == PasteGate::Nothing && !g.reason.empty());
    CHECK(GateClipboard(ReadClipboard("   \n\t  "), false).gate == PasteGate::Nothing);   // blank

    g = GateClipboard(ReadClipboard("1.5"), false);
    CHECK(g.gate == PasteGate::Ok && g.text == "1.5" && g.reason.empty());
    g = GateClipboard(ReadClipboard("  1.5 \r\n"), false);                // a line copied from an editor: its line end and spaces go
    CHECK(g.gate == PasteGate::Ok && g.text == "1.5");

    // Too long: refused with a message that names the limit.
    const std::string big(kMaxClipboardChars + 10, '7');
    g = GateClipboard(ReadClipboard(big.c_str()), false);
    CHECK(g.gate == PasteGate::TooLong && Has(g.reason, "64 KB") && g.text.empty());
    g = GateClipboard(ReadClipboard(big.c_str()), true);                   // a multi-line text box has the same cap
    CHECK(g.gate == PasteGate::TooLong);
    const std::string atCap(kMaxClipboardChars, '7');
    CHECK(GateClipboard(ReadClipboard(atCap.c_str()), true).gate == PasteGate::Ok);

    // Several lines: refused for a one-line field, taken (inner line ends kept) by a multi-line one.
    g = GateClipboard(ReadClipboard("one\ntwo"), false);
    CHECK(g.gate == PasteGate::SeveralLines && Has(g.reason, "several lines") && g.text.empty());
    CHECK(GateClipboard(ReadClipboard("one\r\ntwo"), false).gate == PasteGate::SeveralLines);
    g = GateClipboard(ReadClipboard("one\ntwo\n"), true);
    CHECK(g.gate == PasteGate::Ok && g.text == "one\ntwo");
    // A tab or another control character inside a one-line text.
    g = GateClipboard(ReadClipboard("a\tb"), false);
    CHECK(g.gate == PasteGate::ControlChars && Has(g.reason, "control"));
    g = GateClipboard(ReadClipboard("a\x01" "b"), false);
    CHECK(g.gate == PasteGate::ControlChars);
    CHECK(GateClipboard(ReadClipboard("a\tb"), true).gate == PasteGate::Ok);   // a multi-line box takes a tab
    // Text with letters from other languages is not a control character.
    CHECK(GateClipboard(ReadClipboard("caf\xC3\xA9 \xE2\x82\xAC"), false).gate == PasteGate::Ok);
}

void TestPreview() {
    RUN_SUITE("the preview is one short safe line");
    CHECK(PreviewText("1.6 0.6 3") == "1.6 0.6 3");
    CHECK(PreviewText("a\nb\tc") == "a b c");                              // line ends and tabs show as spaces
    const std::string longText(200, 'x');
    const std::string cut = PreviewText(longText, 48);
    CHECK(cut.size() == 48 + 3 && cut.substr(48) == "...");
    CHECK(PreviewText(std::string(48, 'x'), 48) == std::string(48, 'x'));   // exactly the limit: no dots
    // Never cut inside a multi-byte character: 100 two-byte letters cut at 10 characters is 20 bytes, then dots.
    std::string accents;
    for (int i = 0; i < 100; ++i) accents += "\xC3\xA9";
    const std::string c2 = PreviewText(accents, 10);
    CHECK(c2.size() == 20 + 3 && c2.substr(20) == "...");
    for (std::size_t i = 0; i + 1 < 20; i += 2) CHECK((unsigned char)c2[i] == 0xC3 && (unsigned char)c2[i + 1] == 0xA9);
    CHECK(PreviewText("") == "");
}

void TestHumanSize() {
    RUN_SUITE("sizes in messages");
    CHECK(HumanSize(10) == "10 bytes");
    CHECK(HumanSize(64 * 1024) == "64 KB");
    CHECK(HumanSize(3 * 1024 * 1024 / 2) == "1.5 MB");
}

void TestAssetPaths() {
    RUN_SUITE("a pasted file path has to stay inside the project");
    const std::string posix = "/home/me/Game", win = "C:/Users/me/Game";
    // Project-relative: fine, with either slash.
    for (const char* ok : {"Assets/hero.png", "Assets\\hero.png", "hero.png", "./Assets/hero.png", "Assets/sub folder/a.b.png", "Assets/...x/a.png"}) {
        CHECK(CheckAssetPath(ok, posix).Ok());
        CHECK(CheckAssetPath(ok, "").Ok());          // relative needs no project
    }
    // ".." in any place.
    for (const char* bad : {"..", "../x.png", "Assets/../../x.png", "Assets\\..\\x.png", "a/b/..", "/home/me/Game/../x.png"}) {
        const PathVerdict v = CheckAssetPath(bad, posix);
        CHECK(v.check == PathCheck::ParentFolder && Has(v.reason, ".."));
    }
    // Network paths.
    for (const char* bad : {"\\\\server\\share\\a.png", "//server/share/a.png", "\\\\?\\C:\\x.png"}) {
        const PathVerdict v = CheckAssetPath(bad, win);
        CHECK(v.check == PathCheck::Network && Has(v.reason, "network"));
    }
    // Absolute paths: refused outside the project, accepted inside it (the form the Project panel stores).
    CHECK(CheckAssetPath("/etc/passwd", posix).check == PathCheck::OutsideProject);
    CHECK(CheckAssetPath("/home/me/Game2/a.png", posix).check == PathCheck::OutsideProject);   // a folder whose name only starts the same
    CHECK(CheckAssetPath("/home/me/Game", posix).check == PathCheck::OutsideProject);          // the folder itself is not a file inside it
    CHECK(CheckAssetPath("/home/me/Game/Assets/a.png", posix).Ok());
    CHECK(CheckAssetPath("/home/me/Game/Assets/a.png", "/home/me/Game/").Ok());                 // a trailing slash on the project folder
    CHECK(CheckAssetPath("/home/me/game/Assets/a.png", posix).check == PathCheck::OutsideProject);   // case counts on this kind of path
    CHECK(CheckAssetPath("C:\\Windows\\System32\\x.png", win).check == PathCheck::OutsideProject);
    CHECK(CheckAssetPath("C:\\Users\\me\\Game\\Assets\\a.png", win).Ok());                      // backslashes, same folder
    CHECK(CheckAssetPath("c:/users/ME/game/Assets/a.png", win).Ok());                           // a drive path ignores case
    CHECK(CheckAssetPath("D:/Users/me/Game/Assets/a.png", win).check == PathCheck::OutsideProject);   // another drive
    CHECK(CheckAssetPath("/home/me/Game/Assets/a.png", "").check == PathCheck::NoProject);
    // The strict switch: only project-relative paths pass, even an absolute one inside the project is refused.
    CHECK(CheckAssetPath("Assets/a.png", posix, false).Ok());
    CHECK(CheckAssetPath("/home/me/Game/Assets/a.png", posix, false).check == PathCheck::OutsideProject);
    CHECK(Has(CheckAssetPath("C:\\a.png", win, false).reason, "relative to the project"));
    // Control characters and length.
    CHECK(CheckAssetPath("Assets/a\n.png", posix).check == PathCheck::ControlChars);
    CHECK(CheckAssetPath(std::string("Assets/a") + '\0' + "b.png", posix).check == PathCheck::ControlChars);
    CHECK(CheckAssetPath(std::string(kMaxPastedPathChars, 'a'), posix).Ok());
    CHECK(CheckAssetPath(std::string(kMaxPastedPathChars + 1, 'a'), posix).check == PathCheck::TooLong);
    // Every refusal says something.
    for (const char* bad : {"..", "//s/x", "/etc/x"}) CHECK(!CheckAssetPath(bad, posix).reason.empty());
}

}   // namespace

int main() {
    TestReadClipboard();
    TestGate();
    TestPreview();
    TestHumanSize();
    TestAssetPaths();
    TEST_MAIN_RESULT();
}
