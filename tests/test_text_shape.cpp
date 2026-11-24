// Right-to-left display order and Arabic joining, plus a font atlas that can hold them.
#include "test_framework.hpp"
#include "okay/Core/LocPreview.hpp"
#include "okay/Core/Utf8.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include <string>
#include <vector>

using namespace okay;

static std::uint32_t First(const std::string& s) {
    std::size_t i = 0;
    return utf8::Next(s, i);
}

static bool HasCp(const std::string& s, std::uint32_t cp) {
    for (std::size_t i = 0; i < s.size();) if (utf8::Next(s, i) == cp) return true;
    return false;
}

int main() {
    RUN_SUITE("text_shape");
    CHECK(ShapeForDisplay("ab") == "ab");
    CHECK(ShapeForDisplay("Hello") == "Hello");

    const std::string hebrew = "\u05E9\u05DC\u05D5\u05DD";   // shalom
    const std::string shown = ShapeForDisplay(hebrew);
    CHECK(First(shown) == 0x05DD);   // the last letter is drawn first

    const std::string mixed = ShapeForDisplay(std::string("ab ") + hebrew);
    CHECK(mixed.compare(0, 3, "ab ") == 0);
    CHECK(First(mixed.substr(3)) == 0x05DD);

    const std::string beh = ShapeForDisplay("\u0628\u0628");
    CHECK(HasCp(beh, 0xFE90));   // beh final
    CHECK(HasCp(beh, 0xFE91));   // beh initial
    CHECK(First(beh) == 0xFE90);

    // Every strong left-to-right letter counts as left-to-right, not only Latin. Cyrillic first:
    // the line reads left to right, only the Hebrew word is reversed.
    CHECK(ShapeForDisplay("\u0414\u0430 \u05E9\u05DC") == "\u0414\u0430 \u05DC\u05E9");
    CHECK(ShapeForDisplay("\u0391\u03B2") == "\u0391\u03B2");            // Greek
    CHECK(ShapeForDisplay("\u65E5\u672C") == "\u65E5\u672C");            // CJK
    CHECK(ShapeForDisplay("\u05E9 \u0414") == "\u0414 \u05E9");          // Hebrew first: right-to-left line

    // "ala" (on): ain initial, lam medial, alef maksura final, drawn last letter first.
    {
        const std::string ala = ShapeForDisplay("\u0639\u0644\u0649");
        CHECK(ala == "\uFEF0\uFEE0\uFECB");
    }
    // Yeh with hamza and the Persian letters join too.
    CHECK(ShapeForDisplay("\u0626\u0628") == "\uFE90\uFE8B");
    CHECK(ShapeForDisplay("\u067E\u0686\u06AF") == "\uFB93\uFB7D\uFB58");
    CHECK(ShapeForDisplay("\u06A9\u06CC") == "\uFBFD\uFB90");
    CHECK(ShapeForDisplay("\u0628\u0698") == "\uFB8B\uFE91");

    // A short vowel between the letters must not stop them joining.
    const std::string vowelled = ShapeForDisplay("\u0628\u064E\u0628");
    CHECK(HasCp(vowelled, 0xFE91));
    CHECK(HasCp(vowelled, 0xFE90));

    // The mark stays right after its letter once the run is reversed (a left-to-right renderer
    // draws a mark on the glyph before it).
    {
        std::vector<std::uint32_t> cps;
        for (std::size_t i = 0; i < vowelled.size();) cps.push_back(utf8::Next(vowelled, i));
        CHECK(cps.size() == 3);
        if (cps.size() == 3) {
            CHECK(cps[0] == 0xFE90);      // the second beh, final form, is drawn first
            CHECK(cps[1] == 0xFE91);      // then the first beh, initial form ...
            CHECK(cps[2] == 0x064E);      // ... with its fatha after it
        }
    }
    // Several marks, and a Hebrew point, stay with their letter too.
    {
        const std::string marks = ShapeForDisplay("\u0628\u064E\u0651\u0628");
        std::vector<std::uint32_t> cps;
        for (std::size_t i = 0; i < marks.size();) cps.push_back(utf8::Next(marks, i));
        CHECK(cps.size() == 4 && cps[0] == 0xFE90 && cps[1] == 0xFE91 && cps[2] == 0x064E && cps[3] == 0x0651);
        const std::string pointed = ShapeForDisplay("\u05E9\u05BC\u05DC");
        std::vector<std::uint32_t> h;
        for (std::size_t i = 0; i < pointed.size();) h.push_back(utf8::Next(pointed, i));
        CHECK(h.size() == 3 && h[0] == 0x05DC && h[1] == 0x05E9 && h[2] == 0x05BC);
    }
    // A space ends the join: two words of one beh each stay isolated.
    {
        const std::string words = ShapeForDisplay("\u0628 \u0628");
        CHECK(HasCp(words, 0xFE8F));
        CHECK(!HasCp(words, 0xFE90) && !HasCp(words, 0xFE91));
    }

    // Each line keeps its own order. One reverse of the whole string swapped the lines.
    const std::string two = ShapeForDisplay(hebrew + "\n" + hebrew);
    const std::size_t nl = two.find('\n');
    CHECK(nl != std::string::npos);
    CHECK(First(two.substr(0, nl)) == 0x05DD);
    CHECK(First(two.substr(nl + 1)) == 0x05DD);

    // The draw path's cached shaping: left-to-right text comes back as the same string (no copy), right-to-left
    // text gives what ShapeForDisplay gives, and the second call returns the same cached string.
    {
        const std::string latin = "Start a new game, Score: 123456";
        CHECK(&ShapedForDisplay(latin) == &latin);
        const std::string greek = "\u0391\u03B2\u03B3 \u65E5\u672C";
        CHECK(&ShapedForDisplay(greek) == &greek);
        const std::string crlf = "ab\r\ncd";   // a carriage return is dropped, so this one is shaped
        CHECK(ShapedForDisplay(crlf) == ShapeForDisplay(crlf));
        const std::string arabic = "\u0645\u0631\u062D\u0628\u0627";
        const std::string& once = ShapedForDisplay(arabic);
        CHECK(once == ShapeForDisplay(arabic));
        CHECK(&ShapedForDisplay(arabic) == &once);
        // More distinct strings than the cache holds: each still comes back right.
        for (int k = 0; k < kShapeCacheSize + 20; ++k) {
            const std::string t = hebrew + std::to_string(k);
            CHECK(ShapedForDisplay(t) == ShapeForDisplay(t));
        }
    }

    // The caret on shaped text: the byte of the drawn string the caret sits at.
    {
        CHECK(ShapedCaretByte("abc", 0) == 0);
        CHECK(ShapedCaretByte("abc", 2) == 2);
        CHECK(ShapedCaretByte("abc", 99) == 3);
        // All Hebrew: drawn backwards, so the caret before the first letter is at the right end, after the last at the left.
        CHECK(ShapedCaretByte(hebrew, 0) == hebrew.size());
        CHECK(ShapedCaretByte(hebrew, hebrew.size()) == 0);
        // Before the second letter (lamed, 2 bytes in): lamed is drawn third of four, so the caret is after it, 6 bytes in.
        CHECK(ShapedCaretByte(hebrew, 2) == 6);
        // Latin then Hebrew: the Latin part keeps its offsets; before the first Hebrew letter is the far right.
        const std::string mix = std::string("ab ") + hebrew;
        CHECK(ShapedCaretByte(mix, 1) == 1);
        CHECK(ShapedCaretByte(mix, 3) == mix.size());
        CHECK(ShapedCaretByte(mix, mix.size()) == 3);
        // A mark stays with its letter: a caret on the mark counts as on the letter.
        const std::string pointed = "\u05E9\u05BC\u05DC";   // shin with dagesh, lamed
        CHECK(ShapedCaretByte(pointed, 4) == 2);   // before lamed: lamed is drawn first, caret after it
    }

    TtfFont font;
    const std::string face = std::string(OKAY_SOURCE_DIR) + "/assets/fonts/DejaVuSans.ttf";
    CHECK(font.LoadFromFile(face, 32.0f));
    font.Prepare(hebrew + "\u0628\u0627\u0628");
    std::size_t i = 0;
    const std::uint32_t he = utf8::Next(hebrew, i);
    CHECK(font.GlyphFor(he) != nullptr);
    CHECK(font.GlyphFor(0x0628) != nullptr);
    CHECK(font.Atlas().Width() >= 256 && font.Atlas().Height() >= 256);

    // A tall bake must not grow the atlas past 8192. Six doublings from 512 used to reach 16384.
    TtfFont tall;
    tall.LoadFromFile(face, 360.0f);
    CHECK(tall.Atlas().Width() <= 8192);
    CHECK(tall.Atlas().Height() <= 8192);
    // A bake that needs more than 8192 (but fits 16384) is refused instead of making a 1 GB atlas.
    TtfFont huge;
    const bool hugeOk = huge.LoadFromFile(face, 1500.0f);
    CHECK(!hugeOk || (huge.Atlas().Width() <= 8192 && huge.Atlas().Height() <= 8192));
    TEST_MAIN_RESULT();
}
