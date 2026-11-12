// Renders a window of OkayUI widgets headlessly to a PNG file, after a scripted interaction (button clicks,
// a slider drag, typing, an open combo with a hovered row). No window, no GPU: OkayUI's draw list is
// rasterised by the software backend.
//   okayui_headless_demo [out.png] [classic|launcher]
#include "DemoScene.hpp"

#include <cstring>

int main(int argc, char** argv) {
    using namespace okayuitest;
    const char* out = argc > 1 ? argv[1] : "okayui_demo.png";
    const bool launcher = argc > 2 && !std::strcmp(argv[2], "launcher");
    Reset();
    if (launcher) { OkayUI::UseLauncherLook(true); OkayUI::Style().caretBlink = false; }
    Sim sim(800, 780);
    okayuidemo::State st;
    okayuidemo::RunScript(sim, st);
    const bool ok = WritePng(out, sim.Draw(12, 14, 20));
    std::printf("%s %s (clicks=%d slider=%.2f name=\"%s\")\n", ok ? "wrote" : "FAILED to write", out, st.clicks, (double)st.slider, st.name);
    return ok ? 0 : 1;
}
