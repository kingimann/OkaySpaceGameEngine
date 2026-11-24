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
