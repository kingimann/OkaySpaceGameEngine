// The launcher's themes (shared/OkayTheme.hpp): every theme (Slate, Midnight, Light) defines every token, text and status colours
// are readable (WCAG 4.5:1 or better) on every surface of their theme, the themes really differ, and no launcher page has a
// hard-coded colour: page code asks for a token (okaytheme::kRaised, TextU32, Ink, DangerU32 ...). A colour that must stay the same
// in every theme (artwork, brand colours, the accent presets) sits in a `theme-allow-begin: <reason>` ... `theme-allow-end` block
// or has `theme-allow: <reason>` on its line. Owner bug 2026-11-25: on Light the cards stayed dark while their text turned dark.
#include "test_framework.hpp"
#include "OkayTheme.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace T = okaytheme;
namespace fs = std::filesystem;

static float Dist(const ImVec4& a, const ImVec4& b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z); }

int main() {
    RUN_SUITE("launcher_theme");
    const T::Palette pals[3] = {T::SlatePalette(), T::MidnightPalette(), T::LightPalette()};
    for (const T::Palette& P : pals) {
        const ImVec4 surfaces[] = {P.deep, P.sunken, P.base, P.raised, P.field, P.control};
        for (const ImVec4& s : surfaces) {
            CHECK(T::Contrast(P.text, s) >= 7.0f);       // body text: well past 4.5:1
            CHECK(T::Contrast(P.textDim, s) >= 4.5f);    // secondary text too
            for (const ImVec4& c : {P.success, P.danger, P.warning, P.info}) CHECK(T::Contrast(c, s) >= 4.5f);
        }
        // Light means light surfaces and dark text (the bug: dark cards with dark text).
        CHECK(P.light == (T::Luminance(P.raised) > 0.5f));
        CHECK(P.light == (T::Luminance(P.text) < 0.2f));
        // The cards stand out from the page, the inputs from the cards.
        CHECK(Dist(P.raised, P.base) > 0.04f);
        CHECK(Dist(P.field, P.raised) > 0.02f || P.light);
        CHECK(T::Contrast(P.onColor, T::DangerFill()) >= 3.0f && T::Contrast(P.onColor, T::SuccessFill()) >= 3.0f);
    }
    // The themes are clearly different (owner: Slate and Midnight looked the same).
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j) {
            CHECK(Dist(pals[i].base, pals[j].base) > 0.12f);
            CHECK(Dist(pals[i].raised, pals[j].raised) > 0.12f);
            CHECK(Dist(pals[i].sunken, pals[j].sunken) > 0.12f);
        }
    // SetPalette changes the live tokens; Ink is white on the dark themes and dark on Light.
    T::SetPalette(pals[2]);
    CHECK(Dist(T::kRaised, pals[2].raised) < 1e-6f && Dist(T::kText, pals[2].text) < 1e-6f);
    CHECK(((T::Ink(40) >> IM_COL32_R_SHIFT) & 0xFF) < 64);
    T::SetPalette(pals[0]);
    CHECK(((T::Ink(40) >> IM_COL32_R_SHIFT) & 0xFF) == 255);

    // ---- No hard-coded colour in the launcher's pages ----
    const std::regex col32(R"(IM_COL32\(\s*\d)");
    const std::regex vec4(R"(ImVec4\s*\(\s*([0-9.]+)f?\s*,\s*([0-9.]+)f?\s*,\s*([0-9.]+)f?\s*,\s*([0-9.]+)f?\s*\))");
    const std::regex named(R"(ImVec4\s+[A-Za-z_]\w*\s*\(\s*[0-9.]+f?\s*,)");
    int files = 0, bad = 0;
    for (const auto& e : fs::directory_iterator(fs::path(OKAY_SOURCE_DIR) / "launcher")) {
        const std::string ext = e.path().extension().string();
        if (ext != ".cpp" && ext != ".hpp") continue;
        ++files;
        std::ifstream in(e.path());
        std::string line; int n = 0; bool region = false;
        while (std::getline(in, line)) {
            ++n;
            if (line.find("theme-allow-begin") != std::string::npos) region = true;
            if (line.find("theme-allow-end") != std::string::npos) { region = false; continue; }
            if (region || line.find("theme-allow") != std::string::npos) continue;
            const std::size_t c = line.find("//");
            const std::string code = c == std::string::npos ? line : line.substr(0, c);
            bool hit = std::regex_search(code, col32) || std::regex_search(code, named);
            for (std::sregex_iterator it(code.begin(), code.end(), vec4), end; it != end && !hit; ++it) {
                const float r = std::stof((*it)[1]), g = std::stof((*it)[2]), b = std::stof((*it)[3]), a = std::stof((*it)[4]);
                if (r > 1 || g > 1 || b > 1 || a > 1) continue;   // not a colour
                if (r == 0 && g == 0 && b == 0) continue;         // black / clear: a shadow or "no fill" in every theme
                hit = true;
            }
            if (hit) { ++bad; std::printf("  hard-coded colour: %s:%d: %s\n", e.path().filename().string().c_str(), n, code.c_str()); }
        }
    }
    CHECK(files > 10);
    CHECK(bad == 0);
    TEST_MAIN_RESULT();
}
