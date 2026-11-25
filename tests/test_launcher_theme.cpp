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
#include <iterator>
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

    // ---- The time, "edited" and the ticks on MY chat bubble are readable in every theme ----
    // Owner bug 2026-11-25: on Light, "15:09 · edited ✓✓" on my blue bubble could not be read (it was the theme's info blue at
    // 60% over the blue bubble). The bubble fills are the ones people can pick: every accent preset's dim colour (the default
    // bubble, launcher/LauncherShared.hpp) and every chat theme colour (launcher/main.cpp), read from the source.
    auto readBlock = [](const fs::path& file, const char* marker, int perLine) {
        std::vector<ImVec4> out;
        std::ifstream in(file);
        std::string line; bool on = false;
        const std::regex v(R"(ImVec4\(\s*([0-9.]+)f?\s*,\s*([0-9.]+)f?\s*,\s*([0-9.]+)f?\s*,\s*([0-9.]+)f?\s*\))");
        while (std::getline(in, line)) {
            if (line.find(marker) != std::string::npos) { on = true; continue; }
            if (!on) continue;
            if (line.find("theme-allow-end") != std::string::npos) break;
            int k = 0;
            for (std::sregex_iterator it(line.begin(), line.end(), v), end; it != end; ++it, ++k)
                if (perLine == 0 || k == perLine - 1)
                    out.push_back(ImVec4(std::stof((*it)[1]), std::stof((*it)[2]), std::stof((*it)[3]), 1.0f));
        }
        return out;
    };
    const fs::path src(OKAY_SOURCE_DIR);
    std::vector<ImVec4> fills = readBlock(src / "launcher/LauncherShared.hpp", "theme-allow-begin: the accent colours people pick", 2);
    const std::size_t nAccents = fills.size();
    for (const ImVec4& c : readBlock(src / "launcher/main.cpp", "theme-allow-begin: the chat bubble colours people pick", 0)) fills.push_back(c);
    CHECK(nAccents == 7);
    CHECK(fills.size() == nAccents + 8);
    auto over = [](const ImVec4& fg, const ImVec4& bg) { return T::Mix(ImVec4(bg.x, bg.y, bg.z, 1), ImVec4(fg.x, fg.y, fg.z, 1), fg.w); };
    const ImVec4 defaultBubble = fills.empty() ? ImVec4(0, 0, 0, 1) : fills[0];   // "Classic", the default accent
    int lowFills = 0;
    for (const T::Palette& P : pals) {
        T::SetPalette(P);
        const ImVec4 oldMeta = T::WithAlpha(P.info, 0.6f);   // what the bubble's time used before the fix
        const ImVec4 meta = T::OnFillSoft(defaultBubble);
        std::printf("  %-8s my-bubble time: before %.2f:1, after %.2f:1 (alpha %.2f)\n", P.name,
                    T::Contrast(over(oldMeta, defaultBubble), defaultBubble), T::Contrast(over(meta, defaultBubble), defaultBubble), meta.w);
        CHECK(T::Contrast(over(meta, defaultBubble), defaultBubble) >= 4.5f);
        for (const ImVec4& f : fills) {
            const ImVec4 m = T::OnFillSoft(f);
            CHECK(m.x == P.onColor.x && m.y == P.onColor.y && m.z == P.onColor.z);
            CHECK(m.w >= 0.72f && m.w <= 1.0f);
            if (T::Contrast(P.onColor, f) >= 4.5f) CHECK(T::Contrast(over(m, f), f) >= 4.5f);
            else { CHECK(m.w == 1.0f); ++lowFills; }   // a fill too light for 4.5:1 white: as readable as the body text
        }
    }
    std::printf("  (%d theme x fill pairs where even full white is under 4.5:1; those keep full white)\n", lowFills);
    T::SetPalette(pals[0]);
    // The chat code asks for that colour: no bubble time is drawn in the info blue any more.
    {
        std::ifstream in(src / "launcher/main.cpp");
        const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(all.find("WithAlpha(okaytheme::Current().info, 0.6f)") == std::string::npos);
        CHECK(all.find("OnFillSoft(") != std::string::npos);
    }
    TEST_MAIN_RESULT();
}
