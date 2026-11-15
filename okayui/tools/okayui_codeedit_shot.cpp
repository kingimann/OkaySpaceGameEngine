// A headless picture of OkayUI::CodeEdit showing a script: okayui_codeedit_shot <script.okay> <out.png> [launcher|classic] [scale]
// No window and no GPU: the editor is drawn by OkayUI into draw data and the software render backend turns that into pixels. It shows what
// the own code editor draws (colours, gutter, a breakpoint, an error mark, find matches, a matching bracket, a selection, a hover tooltip
// for a built-in name); it is a render, not a screenshot of the running script editor. The "launcher" look is the editor's default look
// (Roboto, proportional: glyphs sit on the cell grid); "classic" is the 8x8 bitmap font. See docs/own_code_editor.md, "How to try it".
// Written from OkayUI's own public API.
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"
#include "okay/UI/OkayUIRender.hpp"
#include "okay/UI/OkayUIRenderBackends.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "OkayUI_Soft.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace UI = OkayUI;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: okayui_codeedit_shot <script.okay> <out.png> [launcher|classic] [scale]\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    std::ostringstream ss; ss << in.rdbuf();
    const std::string text = ss.str();
    const bool launcher = argc < 4 || std::strcmp(argv[3], "classic") != 0;
    const float scale = argc > 4 ? (float)std::atof(argv[4]) : 1.0f;

    okayuitest::Reset();
    if (launcher) UI::UseLauncherLook(true);
    UI::Style().caretBlink = false;

    UI::CodeEdit ce;
    ce.SetLanguage(UI::OkayScriptLanguage());
    // the docs the script editor gives its widget: built-in names with their signature, shown as a tooltip and coloured as known words
    ce.SetIdentifierDocs({{"Log", "Log(message)\nWrites a line to the Console."}, {"GetAxis", "Input.GetAxis(name) -> float\nThe value of an input axis, -1 to 1."},
                          {"deltaTime", "Time.deltaTime -> float\nSeconds since the last frame."}});
    ce.SetText(text);
    auto lineOf = [&](const char* needle) {
        const std::size_t at = text.find(needle);
        return at == std::string::npos ? -1 : (int)std::count(text.begin(), text.begin() + (std::ptrdiff_t)at, '\n');
    };
    const int errLine = lineOf("StartCoroutine(Hop())");
    const int bpLine = lineOf("hops++");
    const int logLine = lineOf("Debug.Log");
    ce.SetInlineMessages(true);                                                       // the error text next to the code, as the script editor shows it
    if (errLine >= 0) ce.SetMarkers({UI::CodeMarker{errLine, 0, -1, "example error mark: a red bar, a wash and this message on hover"}});
    if (bpLine >= 0) ce.SetBreakpoints({bpLine});
    ce.SetFind("transform", true);
    if (logLine >= 0) ce.SetCaret(UI::CodePos{logLine, (int)ce.Line(logLine).find('(') + 1});        // next to a bracket: its partner is drawn
    const int selLine = lineOf("Vector3(h");
    const std::size_t selCol = selLine >= 0 ? ce.Line(selLine).find("Vector3(h") : std::string::npos;
    if (selCol != std::string::npos) ce.SetSelection(UI::CodePos{selLine, (int)selCol}, UI::CodePos{selLine, (int)selCol + 17});

    const float W = launcher ? 900.0f : 1320.0f, H = 640.0f;     // the 8x8 font is wider: a wider window shows the same columns
    okayuitest::Sim sim(W, H);
    bool open = true;
    auto frame = [&] {
        sim.Frame([&] {
            if (UI::Begin("Script Editor: highlight_sample.okay", 10, 10, W - 20, H - 20, &open)) { ce.Focus(); ce.Draw("ed", W - 40, H - 60); }
            UI::End();
        });
    };
    frame(); frame();
    // rest the pointer on "Log" in the Debug.Log line: the hover tooltip
    if (logLine >= 0) {
        const std::size_t at = ce.Line(logLine).find("Log");
        const UI::Vec2 p = ce.PosToScreen(UI::CodePos{logLine, (int)at});
        sim.Move(p.x + ce.CharWidth() * 1.5f, p.y + ce.LineHeight() * 0.5f);
        frame(); frame();
    }

    const int fw = (int)(W * scale), fh = (int)(H * scale);
    UI::soft::Bitmap bm(fw, fh);
    bm.Clear(12, 14, 20);
    auto be = UI::render::CreateSoftBackend(bm);
    UI::render::Renderer r(*be);
    UI::render::FrameInfo fi; fi.fbWidth = fw; fi.fbHeight = fh; fi.dpi = scale;
    const UI::render::Stats st = r.Render(UI::GetDrawData(), fi);
    if (st.refused) { std::fprintf(stderr, "frame refused: %s\n", st.message.c_str()); return 1; }
    if (!okay::ImageWrite::WritePNG(argv[2], bm.w, bm.h, 4, bm.rgba.data())) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    std::printf("wrote %s (%dx%d, %s look, %d lines, hover tip: %s)\n", argv[2], bm.w, bm.h, launcher ? "launcher" : "classic", ce.LineCount(),
                ce.HoverTip().empty() ? "none" : "shown");
    return 0;
}
