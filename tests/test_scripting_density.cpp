#include "test_framework.hpp"
#include "ScriptingDensity.hpp"
#include "EditorLayoutMath.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// The Density choice (Compact / Comfortable / Spacious) shared by the Visual Script window and the Script editor
// (editor/ScriptingDensity.hpp, no ImGui): the numbers every pane reads, the node-width maths that stops titles, pin labels
// and the status badge from running into each other, text eliding, and that the choice is kept in okay_settings.txt.
using namespace oked::scriptui;

namespace {
std::string Slurp(const char* rel) {
#ifdef OKAY_SOURCE_DIR
    std::ifstream in(std::filesystem::path(OKAY_SOURCE_DIR) / rel);
    std::stringstream ss; ss << in.rdbuf();
    return ss.str();
#else
    (void)rel; return {};
#endif
}
}  // namespace

int main() {
    RUN_SUITE("scripting_density");

    {   // The choice: three steps, Comfortable by default, junk clamps to a real step.
        CHECK(ClampDensity(-5) == 0 && ClampDensity(0) == 0 && ClampDensity(1) == 1 && ClampDensity(2) == 2 && ClampDensity(9) == 2);
        CHECK(std::string(DensityName(0)) == "Compact" && std::string(DensityName(1)) == "Comfortable" && std::string(DensityName(2)) == "Spacious");
        CHECK(std::string(DensityName(77)) == "Spacious");
        CHECK(kDefaultDensity == 1);
        CHECK(DensityCount() == 3);
    }

    {   // Every number grows (or stays) from Compact to Comfortable to Spacious, and is positive.
        const Metrics c = ComputeMetrics(0), m = ComputeMetrics(1), s = ComputeMetrics(2);
        auto up = [](float a, float b, float c2) { return a > 0 && a <= b && b <= c2; };
        CHECK(up(c.nodeW, m.nodeW, s.nodeW) && up(c.headerH, m.headerH, s.headerH) && up(c.rowH, m.rowH, s.rowH));
        CHECK(up(c.pinR, m.pinR, s.pinR) && up(c.pinHit, m.pinHit, s.pinHit) && up(c.nodePad, m.nodePad, s.nodePad));
        CHECK(up(c.codeLineSpacing, m.codeLineSpacing, s.codeLineSpacing) && up(c.codeZoom, m.codeZoom, s.codeZoom));
        CHECK(up(c.sideRowPad, m.sideRowPad, s.sideRowPad) && up(c.sideListW, m.sideListW, s.sideListW));
        CHECK(up(c.paletteSideW, m.paletteSideW, s.paletteSideW) && up(c.paletteRowPad, m.paletteRowPad, s.paletteRowPad));
        CHECK(up(c.tabPad.y, m.tabPad.y, s.tabPad.y) && up(c.framePad.y, m.framePad.y, s.framePad.y) && up(c.itemSpacing.y, m.itemSpacing.y, s.itemSpacing.y));
        CHECK(up(c.problemsRowPad, m.problemsRowPad, s.problemsRowPad) && up(c.smallHit, m.smallHit, s.smallHit));
        // Comfortable is roomier than the old fixed numbers (the owner found those too compact): old sidebar spacing was 4 x 1,
        // old pin buttons in the palette 6 x 2, old palette sidebar 262 wide, old fold chevron 22 wide.
        CHECK(m.paletteSideItemSpacing.y > 1.0f && m.paletteFilterPad.y > 2.0f && m.paletteSideW > 262.0f && m.smallHit > 22.0f);
        CHECK(m.paletteSideRowPad > 0.0f);
        // Compact never goes below a usable minimum, whatever the density.
        CHECK(c.pinHit >= 24.0f && c.smallHit >= 22.0f && c.rowH >= 34.0f);
    }

    {   // The Script editor's side list and fold strip leave room for the longest name at every density.
        for (int d = 0; d < 3; ++d) {
            const Metrics m = ComputeMetrics(d);
            CHECK(m.sideListW >= 240.0f && m.sideFoldW >= 70.0f && m.sideRowH(18.0f) >= 18.0f + 2.0f * m.sideRowPad);
            CHECK(m.minimapW >= 80.0f);
            CHECK(m.codeLineSpacing >= 1.3f && m.codeLineSpacing <= 2.0f);
        }
    }

    {   // Node width: never narrower than the base, wide enough for the title AND the status badge beside it, and for the
        // data-input labels + value box + output labels on one row. The badge used to be drawn over a long title.
        const Metrics m = ComputeMetrics(1);
        NodeWidthIn in; in.titleW = 100; in.badgeW = 50; in.hasDataIn = false;
        CHECK(NodeWidth(m, in) == m.nodeW);                         // short title: the base
        in.titleW = m.nodeW;                                        // a title as wide as the node: the badge does not fit beside it
        CHECK(NodeWidth(m, in) >= in.titleW + in.badgeW + 2.0f * m.nodePad);
        in = NodeWidthIn{}; in.titleW = 120; in.badgeW = 60;
        CHECK(NodeWidth(m, in) >= 120 + 60 + 2.0f * m.nodePad);
        in = NodeWidthIn{}; in.hasDataIn = true; in.inLabelW = 90; in.outLabelW = 80; in.boxW = m.boxW;
        CHECK(NodeWidth(m, in) >= m.nodePad + 90 + m.pinLabelGap + m.boxW + m.nodePad + 80 + m.nodePad);
        in = NodeWidthIn{}; in.titleW = 5000; in.badgeW = 50;       // an absurd title is capped; it is elided, not drawn out
        CHECK(NodeWidth(m, in) == m.nodeMaxW);
        CHECK(m.nodeMaxW >= m.nodeW * 1.5f);
        in = NodeWidthIn{}; in.zoom = 0.5f; in.titleW = 10;
        CHECK(NodeWidth(m, in) == m.nodeW * 0.5f);                  // everything scales with the canvas zoom
    }

    {   // Eliding: fits -> unchanged; too long -> cut with "..." and the result fits; nothing fits -> empty; UTF-8 not split.
        auto measure = [](const std::string& s) { return 10.0f * (float)s.size(); };
        CHECK(ElideToWidth("Hello", 100.0f, measure) == "Hello");
        const std::string e = ElideToWidth("Hello world, long title", 100.0f, measure);
        CHECK(e.size() <= 10 && e.size() >= 4 && e.substr(e.size() - 3) == "..." && measure(e) <= 100.0f);
        CHECK(ElideToWidth("Hello", 5.0f, measure).empty());
        CHECK(ElideToWidth("", 50.0f, measure).empty());
        const std::string u = ElideToWidth("a\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 80.0f, measure);
        bool bad = false;   // no lone continuation byte at the cut
        for (std::size_t i = 0; i < u.size(); ++i) if (((unsigned char)u[i] & 0xC0) == 0x80 && (i == 0 || (unsigned char)u[i - 1] < 0x80)) bad = true;
        CHECK(!bad);
    }

    {   // The choice is kept with the other editor settings.
        okay::edlayout::ViewPrefs p;
        CHECK(p.scriptDensity == kDefaultDensity);
        CHECK(p.Apply("scriptdensity", 2) && p.scriptDensity == 2);
        CHECK(p.Apply("scriptdensity", 99) && p.scriptDensity == 2);
        CHECK(p.Apply("scriptdensity", -3) && p.scriptDensity == 0);
        p.scriptDensity = 2;
        const okay::edlayout::ViewPrefs q = okay::edlayout::ViewPrefs::Parse(p.Write());
        CHECK(q.scriptDensity == 2);
        CHECK(okay::edlayout::ViewPrefs::Parse("").scriptDensity == 1);
    }

    {   // The pending-change hand-off: a menu sets the density; the host takes the change once and saves it.
        g_scriptDensity = 1; g_scriptDensityChanged = false;
        SetDensity(1);
        CHECK(!TakeDensityChanged());                               // same value: nothing to save
        SetDensity(2);
        CHECK(g_scriptDensity == 2 && TakeDensityChanged() && !TakeDensityChanged());
        SetDensity(40);
        CHECK(g_scriptDensity == 2);                                // clamped, so no change
        g_scriptDensity = 1;
    }

#ifdef OKAY_SOURCE_DIR
    {   // Wiring: the Visual Script app reads the shared numbers (nobody keeps a private copy), its View menu has the picker, and the
        // editor saves a changed choice. (The editor's Dear ImGui Visual Script window and its palette read them before the app
        // replaced them.)
        const std::string canvas = Slurp("shared/VisualScriptAppCanvas.cpp"), app = Slurp("shared/VisualScriptApp.cpp"), mn = Slurp("editor/main.cpp");
        CHECK(canvas.find("scriptui::Current()") != std::string::npos);
        CHECK(canvas.find("292.0f * z") == std::string::npos);      // no fixed node size
        CHECK(app.find("scriptui::SetDensity(") != std::string::npos && app.find("scriptui::DensityName(") != std::string::npos);
        CHECK(mn.find("TakeDensityChanged") != std::string::npos);
    }
#endif
    TEST_MAIN_RESULT();
}
