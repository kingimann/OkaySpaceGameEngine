// Renders a window of okay::imui widgets headlessly to a PNG file, after a scripted interaction (button
// clicks, a slider drag, typing, an open combo with a hovered row), so the picture shows live state.
//   imui_demo [out.png]
#include "DemoScene.hpp"

int main(int argc, char** argv) {
    using namespace imuitest;
    const char* out = argc > 1 ? argv[1] : "imui_demo.png";
    Context* ctx = MakeContext();
    Sim sim(ctx, Vec2(760, 520));
    imuidemo::State st;
    imuidemo::RunScript(sim, st);
    const bool ok = WritePng(out, sim.Draw(MakeCol(12, 14, 20)));
    std::printf("%s %s (clicks=%d slider=%.2f name=\"%s\")\n", ok ? "wrote" : "FAILED to write", out, st.clicks, (double)st.slider, st.name.c_str());
    DestroyContext(ctx);
    return ok ? 0 : 1;
}
