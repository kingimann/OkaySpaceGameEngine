#include "test_framework.hpp"
#include <okay/Inventory/TextFit.hpp>
#include <string>

int main() {
    using okay::InventoryText::FitWidth;
    auto m = [](const std::string& s) {   // 10 px per code point
        float n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n += 10.0f; return n; };
    CHECK(FitWidth("Iron", 100, m) == "Iron");
    CHECK(FitWidth("Iron", 40, m) == "Iron");                 // exactly fits
    CHECK(FitWidth("Iron Sword", 100, m) == "Iron Sword");    // exactly 100
    CHECK(FitWidth("Iron Sword of Doom", 100, m) == "Iron Sw...");
    CHECK(m(FitWidth("Iron Sword of Doom", 100, m)) <= 100.0f);
    CHECK(FitWidth("Iron Sword of Doom", 30, m) == "...");
    CHECK(FitWidth("Iron Sword of Doom", 29, m).empty());
    CHECK(FitWidth("Iron", 0, m).empty());
    CHECK(FitWidth("Iron", -5, m).empty());
    CHECK(FitWidth("", 5, m).empty());
    // UTF-8: never cut inside a code point (e-acute is 2 bytes, CJK 3).
    CHECK(FitWidth("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 50, m) == "\xC3\xA9\xC3\xA9...");
    CHECK(FitWidth("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 60, m)
          == "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E...");
    bool all = true;   // every width gives a result that fits
    for (float w = 0; w < 200; w += 3.0f) {
        const auto r = FitWidth("Enchanted Dragonbone Greatsword", w, m);
        if (m(r) > w) all = false;
    }
    CHECK(all);
    TEST_MAIN_RESULT();
}
