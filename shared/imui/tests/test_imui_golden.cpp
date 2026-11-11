// okay::imui golden images: committed PNGs (written with okay::ImageWrite, read back with the test kit's own
// inflate) compared against the software backend's output. IMUI_UPDATE_GOLDEN=1 regenerates them.
#include "test_framework.hpp"
#include "../demo/DemoScene.hpp"

using namespace imuitest;

static void Check(const char* name, const Bitmap& b) {
    std::string why;
    const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << " (actual written as " << name << ".actual.png)\n";
    CHECK(ok);
}

int main() {
    RUN_SUITE("imui golden");
    Context* ctx = MakeContext();
    {   // draw list primitives
        DrawList dl; dl.Init(&ctx->font, Rect(0, 0, 200, 120));
        dl.AddRectFilled(Vec2(8, 8), Vec2(60, 40), MakeCol(200, 60, 60));
        dl.AddRectFilled(Vec2(68, 8), Vec2(120, 40), MakeCol(60, 160, 80), 10);
        dl.AddRect(Vec2(128, 8), Vec2(192, 40), MakeCol(240, 240, 80), 8, 2);
        dl.AddRect(Vec2(8, 48), Vec2(60, 80), MakeCol(255, 255, 255), 0, 1);
        dl.AddLine(Vec2(68, 48), Vec2(120, 80), MakeCol(120, 200, 255), 3);
        dl.AddTriangleFilled(Vec2(128, 80), Vec2(160, 48), Vec2(192, 80), MakeCol(255, 150, 40));
        dl.AddCircleFilled(Vec2(24, 100), 12, MakeCol(180, 100, 255));
        dl.PushClipRect(Rect(50, 90, 120, 112));
        dl.AddText(Vec2(46, 92), MakeCol(255, 255, 255), "Clipped text");
        dl.PopClipRect();
        dl.AddRectFilled(Vec2(130, 90), Vec2(190, 112), MakeCol(255, 255, 255, 90));
        dl.AddText(Vec2(134, 94), MakeCol(255, 255, 255), "UTF \xC3\xA9");
        DrawData dd; dd.lists.push_back(&dl);
        Bitmap bm(200, 120); bm.Clear(MakeCol(20, 22, 30));
        SoftRender(dd, ctx->font, bm);
        Check("primitives", bm);
    }
    {   // a button in its three states
        Sim sim(ctx, Vec2(300, 70));
        Rect r[3];
        auto ui = [&] {
            SetNextWindowPos(Vec2(0, 0), Cond_Once); SetNextWindowSize(Vec2(300, 70), Cond_Once);
            Begin("states", nullptr, Win_NoTitleBar | Win_NoResize | Win_NoMove);
            Button("Idle"); r[0] = GetItemRect(); SameLine();
            Button("Hover"); r[1] = GetItemRect(); SameLine();
            Button("Held"); r[2] = GetItemRect();
            End();
        };
        sim.Frames(2, ui);
        sim.Move(Center(r[2]).x, Center(r[2]).y); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        const Bitmap& held = sim.Draw(MakeCol(12, 14, 20));
        auto px = [&](const Bitmap& b, const Rect& rr) { const unsigned char* q = &b.rgba[((size_t)(rr.min.y + 3) * b.w + (size_t)(rr.min.x + 3)) * 4]; return MakeCol(q[0], q[1], q[2], q[3]); };
        CHECK(px(held, r[0]) == ctx->style.colors[Col_Button]);
        CHECK(px(held, r[2]) == ctx->style.colors[Col_ButtonActive]);
        Check("button_held", held);
        sim.Up(); sim.Move(Center(r[1]).x, Center(r[1]).y); sim.Frames(2, ui);
        const Bitmap& hov = sim.Draw(MakeCol(12, 14, 20));
        CHECK(px(hov, r[1]) == ctx->style.colors[Col_ButtonHovered] && px(hov, r[2]) == ctx->style.colors[Col_Button]);
        Check("button_hover", hov);
    }
    {   // the demo window after its scripted interaction
        Sim sim(ctx, Vec2(760, 520));
        imuidemo::State st;
        imuidemo::RunScript(sim, st);
        CHECK(st.clicks == 3 && std::fabs(st.slider - 0.7f) < 0.02f && st.name == "Okay Space 2026");
        Check("demo_window", sim.Draw(MakeCol(12, 14, 20)));
    }
    {   // determinism: the same frames render the same pixels
        Bitmap a, b;
        for (int pass = 0; pass < 2; ++pass) {
            Context* c2 = MakeContext();
            Sim sim(c2, Vec2(760, 520));
            imuidemo::State st;
            imuidemo::RunScript(sim, st);
            (pass == 0 ? a : b) = sim.Draw(MakeCol(12, 14, 20));
            DestroyContext(c2);
        }
        CHECK(a.rgba == b.rgba);
    }
    {   // our PNG reader round-trips what ImageWrite wrote
        Bitmap bm(33, 17); bm.Clear(MakeCol(1, 2, 3));
        for (int i = 0; i < 33 * 17; ++i) bm.rgba[(size_t)i * 4] = (unsigned char)(i * 7);
        const std::string tmp = "imui_roundtrip.png";
        CHECK(WritePng(tmp, bm));
        Png p = ReadPng(tmp);
        CHECK(p.ok && p.w == 33 && p.h == 17 && p.rgba == bm.rgba);
        std::remove(tmp.c_str());
        CHECK(!ReadPng("does-not-exist.png").ok);
    }
    DestroyContext(ctx);
    TEST_MAIN_RESULT();
}
