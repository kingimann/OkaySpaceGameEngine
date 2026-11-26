// The start-up splash (shared/Splash.hpp) drawn headless on the software painter at fixed progress values and
// step names: the bar's fill width matches the progress, the status text and percentage are drawn, the failed
// state turns the bar red, the fade blends over what is under it, the plain frame (no state) still draws its
// sweep where it always did, SplashSteps and SplashFinish behave, and one picture is compared with
// tests/golden/splash/loading_50.png.
//   test_splash                  check
//   test_splash --update <dir>   write loading_50.png into <dir> (review it, then copy it to tests/golden/splash/)
#include "Splash.hpp"
#include "OkayUI_SoftPainter.hpp"
#include "okay/Graphics/Image.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace plat = okay::platform;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static const int kW = 640, kH = 400;

struct Img { int w = 0, h = 0; std::vector<std::uint8_t> px; const std::uint8_t* at(int x, int y) const { return &px[((size_t)y * w + x) * 4]; } };

static okay::SplashStyle Style() {
    okay::SplashStyle st;   // the defaults: accent (60, 121, 204), charcoal backdrop, the cube
    return st;
}

static okay::SplashProgress State(float progress, const std::string& status) {
    okay::SplashProgress s;
    s.title = "Space Game";
    s.status = status;
    s.footer = "OkaySpace Editor  v1.0.0";
    s.progress = progress;
    s.timeMs = 1000.0;   // a fixed animation clock: the same picture every run
    s.present = false;
    return s;
}

static Img Draw(const okay::SplashStyle& st, const okay::SplashProgress* s, std::uint8_t under = 0) {
    Img i;
    std::unique_ptr<plat::Painter> p = OkayUI::soft::CreateBitmapPainter(kW, kH);
    if (!p) return i;
    p->SetDrawColor(under, under, under, 255);
    p->Clear();
    okay::DrawSplashStyled(p.get(), kW, kH, st, s);
    p->ReadPixels(i.px, i.w, i.h);
    return i;
}

static int Dist2(const std::uint8_t* q, int r, int g, int b) { return (q[0] - r) * (q[0] - r) + (q[1] - g) * (q[1] - g) + (q[2] - b) * (q[2] - b); }

// Pixels on the bar's middle row that are the fill (closer to `fill` than to the track's grey).
static int FilledOnBar(const Img& i, const okay::SplashLayout& L, int r, int g, int b) {
    int n = 0;
    const int y = (int)(L.barY + L.barH * 0.5f);
    for (int x = (int)L.barX - 4; x < (int)(L.barX + L.barW) + 4; ++x) {
        if (x < 0 || x >= i.w) continue;
        const std::uint8_t* q = i.at(x, y);
        if (Dist2(q, r, g, b) < Dist2(q, 54, 54, 60)) ++n;
    }
    return n;
}

// Pixels in a box that are much brighter than the backdrop there (text is light grey on charcoal).
static int Bright(const Img& i, int x0, int y0, int x1, int y1, int over = 70) {
    int n = 0;
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < i.h; ++y)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < i.w; ++x) {
            const std::uint8_t* q = i.at(x, y);
            if (q[0] > over || q[1] > over || q[2] > over) ++n;
        }
    return n;
}

static void Layout() {
    okay::SplashProgress s = State(0.5f, "Loading fonts");
    const okay::SplashLayout L = okay::ComputeSplashLayout(kW, kH, &s);
    CHECK(L.text);
    CHECK(std::fabs(L.fillW - std::floor(L.barW * 0.5f + 0.5f)) < 0.01f);
    CHECK(L.barX >= 0 && L.barX + L.barW <= kW);
    CHECK(L.statusY > L.barY + L.barH);
    CHECK(L.titleY > L.cy + 60.0f && L.titleY + L.titlePx < L.barY);   // the title sits between the logo and the bar
    CHECK(L.footerY > L.noteY + L.statusPx);
    s.progress = 1.7f;  CHECK(okay::ComputeSplashLayout(kW, kH, &s).fillW == okay::ComputeSplashLayout(kW, kH, &s).barW);   // clamped
    s.progress = -1.0f; CHECK(okay::ComputeSplashLayout(kW, kH, &s).fillW == 0.0f);                                          // the sweep
    const okay::SplashLayout plain = okay::ComputeSplashLayout(kW, kH, nullptr);   // the launcher's and the player's frame: unchanged
    CHECK(!plain.text);
    CHECK(plain.barW == 210.0f && plain.barY == kH * 0.5f - 26.0f + 118.0f);
}

static void FillWidths() {
    const okay::SplashStyle st = Style();
    const int ar = (int)(st.ar * 255.0f + 0.5f), ag = (int)(st.ag * 255.0f + 0.5f), ab = (int)(st.ab * 255.0f + 0.5f);
    for (float p : {0.0f, 0.1f, 0.25f, 0.5f, 0.8f, 1.0f}) {
        okay::SplashProgress s = State(p, "Starting the GPU renderer");
        const okay::SplashLayout L = okay::ComputeSplashLayout(kW, kH, &s);
        const Img i = Draw(st, &s);
        CHECK(i.w == kW && i.h == kH);
        if (i.w != kW) return;
        const int filled = FilledOnBar(i, L, ar, ag, ab);
        std::printf("  progress %.2f: fill %d px drawn, %.0f px expected (bar %.0f px)\n", p, filled, L.fillW, L.barW);
        CHECK(std::abs(filled - (int)L.fillW) <= 2);
    }
}

static void CheckSplashStatusLine() {
    const okay::SplashStyle st = Style();
    okay::SplashProgress s = State(0.42f, "Opening project Space Game");
    const okay::SplashLayout L = okay::ComputeSplashLayout(kW, kH, &s);
    const Img with = Draw(st, &s);
    s.status.clear(); s.detail = " ";   // no status, and a blank detail instead of the percentage
    const Img without = Draw(st, &s);
    const int sy0 = (int)L.statusY, sy1 = (int)(L.statusY + L.statusPx + 4);
    const int left = Bright(with, (int)L.barX, sy0, (int)(L.barX + L.barW * 0.6f), sy1);
    const int leftNone = Bright(without, (int)L.barX, sy0, (int)(L.barX + L.barW * 0.6f), sy1);
    const int pct = Bright(with, (int)(L.barX + L.barW - 40), sy0, (int)(L.barX + L.barW), sy1);
    const int pctNone = Bright(without, (int)(L.barX + L.barW - 40), sy0, (int)(L.barX + L.barW), sy1);
    std::printf("  status text: %d bright pixels (none: %d), percentage: %d (none: %d)\n", left, leftNone, pct, pctNone);
    CHECK(left > 60 && leftNone == 0);
    CHECK(pct > 20 && pctNone == 0);
    // The title and the footer.
    CHECK(Bright(with, kW / 2 - 80, (int)L.titleY, kW / 2 + 80, (int)(L.titleY + L.titlePx + 6)) > 100);
    CHECK(Bright(with, kW / 2 - 120, (int)L.footerY, kW / 2 + 120, (int)(L.footerY + L.footerPx + 6), 50) > 40);
    // A long status is cut to fit the bar, never drawn past its end.
    s = State(0.42f, "Importing a very long list of things with a name that will never fit on one line under the bar");
    s.detail = " ";
    const Img longer = Draw(st, &s);
    CHECK(Bright(longer, (int)(L.barX + L.barW + 2), sy0, kW, sy1) == 0);
    CHECK(Bright(longer, (int)L.barX, sy0, (int)(L.barX + L.barW), sy1) > 60);
}

static void Failed() {
    const okay::SplashStyle st = Style();
    okay::SplashProgress s = State(0.6f, "Couldn't open Space Game: the scene file is missing");
    s.failed = true;
    const okay::SplashLayout L = okay::ComputeSplashLayout(kW, kH, &s);
    const Img i = Draw(st, &s);
    const int red = FilledOnBar(i, L, 237, 102, 94);
    std::printf("  failed: %d red px on the bar (%.0f expected)\n", red, L.fillW);
    CHECK(std::abs(red - (int)L.fillW) <= 2);
    const std::uint8_t* q = i.at((int)(L.barX + 10), (int)(L.barY + L.barH * 0.5f));
    CHECK(q[0] > q[2] + 60);                                                   // red, not the accent blue
    CHECK(Bright(i, (int)L.barX, (int)L.noteY, (int)(L.barX + L.barW), (int)(L.noteY + L.statusPx + 4), 60) > 40);   // "The editor will open anyway."
    s.failed = false;
    const Img ok = Draw(st, &s);
    CHECK(Bright(ok, (int)L.barX, (int)L.noteY, (int)(L.barX + L.barW), (int)(L.noteY + L.statusPx + 4), 60) == 0);
}

static void Fade() {
    const okay::SplashStyle st = Style();
    okay::SplashProgress s = State(1.0f, "Ready");
    s.opacity = 0.5f;
    const Img i = Draw(st, &s, 255);   // over a white "editor frame"
    const std::uint8_t* corner = i.at(2, 2);   // backdrop top (24, 24, 27) at half opacity over white
    std::printf("  fade 0.5 over white: corner (%d, %d, %d)\n", corner[0], corner[1], corner[2]);
    CHECK(std::abs(corner[0] - (24 + 255) / 2) <= 3 && std::abs(corner[2] - (27 + 255) / 2) <= 3);
    s.opacity = 0.0f;
    const Img gone = Draw(st, &s, 255);
    CHECK(gone.at(kW / 2, kH / 2)[0] == 255 && gone.at(2, 2)[1] == 255);   // nothing drawn at all
}

static void PlainFrame() {
    // No state (the launcher, the player): the indeterminate sweep on the old bar, no text.
    const okay::SplashStyle st = Style();
    const okay::SplashLayout L = okay::ComputeSplashLayout(kW, kH, nullptr);
    okay::SplashProgress fixed; fixed.timeMs = 700.0; fixed.present = false;   // no text: the plain frame with a fixed clock
    const Img i = Draw(st, &fixed);
    const int ar = (int)(st.ar * 255.0f + 0.5f), ag = (int)(st.ag * 255.0f + 0.5f), ab = (int)(st.ab * 255.0f + 0.5f);
    const int sweep = FilledOnBar(i, L, ar, ag, ab);
    std::printf("  plain frame: sweep %d px\n", sweep);
    CHECK(sweep > 20 && sweep <= (int)(L.barW * 0.32f) + 2);
    CHECK(Bright(i, 0, (int)(L.barY + 10), kW, kH, 90) == 0);   // no text under the bar
}

static void Steps() {
    okay::SplashSteps s;
    s.Add("Window", 1); s.Add("Fonts", 2); s.Add("GPU", 3); s.Add("Project", 4);
    CHECK(s.Fraction() == 0.0f);
    s.Begin("Fonts");
    CHECK(std::fabs(s.Fraction() - 0.1f) < 1e-5f && s.Status() == "Fonts");
    s.Begin("Project", "Opening project Space Game");
    CHECK(std::fabs(s.Fraction() - 0.6f) < 1e-5f && s.Status() == "Opening project Space Game");
    s.Begin("GPU");   // an earlier step again: the bar does not go back
    CHECK(std::fabs(s.Fraction() - 0.6f) < 1e-5f);
    s.Begin("Something new");   // unknown: added at the end, with weight 1
    CHECK(s.Count() == 5 && std::fabs(s.Fraction() - 10.0f / 11.0f) < 1e-5f);
    okay::SplashProgress st;
    s.Fail("Couldn't open the project");
    s.Apply(st);
    CHECK(st.failed && st.status == "Couldn't open the project");
    s.Finish();
    s.Apply(st);
    CHECK(st.progress == 1.0f && st.status == "Ready");
}

static void Finish() {
    okay::SplashFinish f;   // fade 280 ms, error hold 1800 ms, max hold 15 s
    CHECK(f.Update(0, true, false) == 1.0f);
    CHECK(f.Update(5000, true, false) == 1.0f);           // still held
    CHECK(f.Update(5100, false, false) == 1.0f);          // released: the fade starts here
    const float a = f.Update(5170, false, false), b = f.Update(5240, false, false);
    CHECK(a < 1.0f && b < a && b > 0.0f);                 // fading, smoothly down
    CHECK(f.Update(5400, false, false) == 0.0f && f.Done());
    CHECK(f.Update(9000, true, false) == 0.0f);           // done stays done

    okay::SplashFinish g;
    g.Update(0, true, false);
    CHECK(g.Update(16000, true, false) == 1.0f);          // the max hold ends now: this frame starts the fade
    CHECK(g.Update(16100, true, false) < 1.0f);

    okay::SplashFinish e;
    e.Update(0, false, true);                             // failed on the first frame: up long enough to read it
    CHECK(e.Update(1700, false, false) == 1.0f);
    CHECK(e.Update(1900, false, false) == 1.0f && e.Fading());
    CHECK(e.Update(2300, false, false) == 0.0f);
}

static void Golden(const std::string& updateDir) {
    okay::SplashProgress s = State(0.5f, "Starting the GPU renderer");
    const Img i = Draw(Style(), &s);
    okay::Image got(i.w, i.h);
    for (size_t k = 0; k < i.px.size(); ++k) got.Data()[k] = i.px[k];
    if (!updateDir.empty()) { CHECK(got.SavePNG(updateDir + "/loading_50.png")); std::printf("  wrote loading_50.png\n"); return; }
    okay::Image want;
    const std::string path = std::string(OKAY_SOURCE_DIR) + "/tests/golden/splash/loading_50.png";
    CHECK(want.Load(path));
    CHECK(want.Width() == got.Width() && want.Height() == got.Height());
    if (want.Width() != got.Width() || want.Height() != got.Height()) return;
    int diff = 0, big = 0, maxd = 0;
    for (int p = 0; p < got.Width() * got.Height(); ++p) {
        int d = 0;
        for (int c = 0; c < 3; ++c) { const int x = std::abs((int)want.Data()[p * 4 + c] - (int)got.Data()[p * 4 + c]); if (x > d) d = x; }
        if (d > 0) ++diff;
        if (d > 8) ++big;
        if (d > maxd) maxd = d;
    }
    std::printf("  golden loading_50: %d differing pixels (max %d), %d by more than 8\n", diff, maxd, big);
    // Another libm can round a few glow and glyph edge pixels differently: allow a sliver, almost none by much.
    CHECK(diff <= 600);
    CHECK(big <= 12);
}

int main(int argc, char** argv) {
    std::string updateDir;
    for (int a = 1; a + 1 < argc; ++a) if (std::string(argv[a]) == "--update") updateDir = argv[a + 1];
    Layout();
    FillWidths();
    CheckSplashStatusLine();
    Failed();
    Fade();
    PlainFrame();
    Steps();
    Finish();
    Golden(updateDir);
    std::printf("%d/%d checks passed\n", g_n - g_fail, g_n);
    return g_fail ? 1 : 0;
}
