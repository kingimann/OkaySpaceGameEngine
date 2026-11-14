// LoadingScreenLayout: BuildLoadingScreen is the draw list the player and the
// editor both paint. These checks pin positions, not just that a list comes back.
#include "test_framework.hpp"
#include "okay/Components/LoadingScreenLayout.hpp"
#include <cmath>

using namespace okay;

namespace {

const LoadingDrawCmd* Find(const std::vector<LoadingDrawCmd>& cmds, LoadingDrawCmd::Kind kind,
                           const std::string& text = {}) {
    for (const auto& d : cmds)
        if (d.kind == kind && (text.empty() || d.text == text)) return &d;
    return nullptr;
}

} // namespace

int main() {
    RUN_SUITE("loading_screen_layout");

    LoadingScreen blank;
    CHECK(BuildLoadingScreen(blank, 0.0f, 100.0f).empty());
    CHECK(BuildLoadingScreen(blank, 100.0f, -5.0f).empty());
    CHECK(BuildLoadingScreen(blank, -1.0f, -1.0f).empty());

    // Classic: the title sits on the centre line at titleY, at the auto size.
    {
        LoadingScreen ls;
        ls.layout = (int)LoadingScreen::Layout::Classic;
        ls.title = "Hello";
        ls.showTitle = true;
        ls.showBar = false;
        ls.showSpinner = false;
        ls.showPercent = false;
        ls.gradientBackground = false;
        ls.backgroundColor = Color{0.1f, 0.2f, 0.3f, 1.0f};
        const float W = 500.0f, H = 500.0f;
        const auto cmds = BuildLoadingScreen(ls, W, H, 0.0f);
        const LoadingDrawCmd* bg = Find(cmds, LoadingDrawCmd::Kind::Rect);
        CHECK(bg != nullptr);
        if (bg) {
            CHECK_NEAR(bg->w, W, 1e-3f);
            CHECK_NEAR(bg->h, H, 1e-3f);
            CHECK_NEAR(bg->color.r, 0.1f, 1e-4f);
            CHECK_NEAR(bg->alpha, 1.0f, 1e-4f);               // a preview is fully opaque
        }
        const LoadingDrawCmd* title = Find(cmds, LoadingDrawCmd::Kind::Text, "Hello");
        CHECK(title != nullptr);
        if (title) {
            CHECK(title->align == 1);
            CHECK_NEAR(title->x, W * 0.5f, 1e-3f);
            CHECK_NEAR(title->y, H * ls.titleY, 1e-3f);
            CHECK_NEAR(title->h, H * 0.048f, 1e-3f);
        }
        CHECK(Find(cmds, LoadingDrawCmd::Kind::Gradient) == nullptr);
    }

    // Minimal preset is a flat background, not a gradient.
    {
        LoadingScreen ls;
        ApplyLoadingPreset(ls, LoadingScreen::Layout::Minimal);
        const auto cmds = BuildLoadingScreen(ls, 800.0f, 600.0f, 0.0f);
        CHECK(Find(cmds, LoadingDrawCmd::Kind::Gradient) == nullptr);
        const LoadingDrawCmd* bg = Find(cmds, LoadingDrawCmd::Kind::Rect);
        CHECK(bg != nullptr);
        if (bg) {
            CHECK_NEAR(bg->w, 800.0f, 1e-3f);
            CHECK_NEAR(bg->h, 600.0f, 1e-3f);
        }
    }

    // Cinematic letterbox bars are black and 12% of the height, top and bottom.
    {
        LoadingScreen ls;
        ls.layout = (int)LoadingScreen::Layout::Cinematic;
        ls.gradientBackground = false;
        ls.backgroundColor = Color{0.4f, 0.4f, 0.4f, 1};
        ls.showBar = false;
        ls.showSpinner = false;
        ls.showTitle = false;
        const float H = 500.0f;
        const auto cmds = BuildLoadingScreen(ls, 1000.0f, H, 0.0f);
        int bars = 0;
        for (const auto& d : cmds) {
            if (d.kind != LoadingDrawCmd::Kind::Rect) continue;
            if (d.color.r > 0.01f || d.color.g > 0.01f || d.color.b > 0.01f) continue;
            if (std::fabs(d.h - H * 0.12f) > 1e-2f) continue;
            ++bars;
            CHECK(d.y < 1.0f || std::fabs(d.y - (H - H * 0.12f)) < 1e-2f);
        }
        CHECK(bars == 2);
    }

    // Ken Burns zooms the background toward the centre over the screen's life.
    {
        LoadingScreen ls;
        ls.backgroundImage = "bg.png";
        ls.kenBurns = true;
        ls.duration = 2.0f;
        ls.showBar = false;
        ls.showSpinner = false;
        ls.showTitle = false;
        const auto at0 = BuildLoadingScreen(ls, 640.0f, 360.0f, 0.0f);
        const LoadingDrawCmd* img0 = Find(at0, LoadingDrawCmd::Kind::Image, "bg.png");
        CHECK(img0 != nullptr);
        if (img0) {
            CHECK_NEAR(img0->u0, 0.0f, 1e-4f);
            CHECK_NEAR(img0->u1, 1.0f, 1e-4f);
        }
        const auto atEnd = BuildLoadingScreen(ls, 640.0f, 360.0f, 2.0f);
        const LoadingDrawCmd* img1 = Find(atEnd, LoadingDrawCmd::Kind::Image, "bg.png");
        CHECK(img1 != nullptr);
        const float z = 1.08f;
        const float m = (1.0f - 1.0f / z) * 0.5f;
        if (img1) {
            CHECK_NEAR(img1->u0, m, 1e-4f);
            CHECK_NEAR(img1->v0, m, 1e-4f);
            CHECK_NEAR(img1->u1, 1.0f - m, 1e-4f);
            CHECK_NEAR(img1->v1, 1.0f - m, 1e-4f);
        }
    }

    // The ring's fill arc starts at the top and covers the progress.
    {
        LoadingScreen ls;
        ls.layout = (int)LoadingScreen::Layout::Ring;
        ls.duration = 2.0f;
        ls.showBar = true;
        ls.showPercent = false;
        ls.showSpinner = false;
        ls.showTitle = false;
        ls.barStyle = (int)LoadingScreen::BarStyle::Solid;
        ls.barFill = Color{0.1f, 0.2f, 0.9f, 1};
        ls.barBackground = Color{0.9f, 0.1f, 0.1f, 1};
        const auto cmds = BuildLoadingScreen(ls, 400.0f, 400.0f, 1.0f);   // half of the 2s duration
        const LoadingDrawCmd* fill = nullptr;
        const LoadingDrawCmd* track = nullptr;
        for (const auto& d : cmds) {
            if (d.kind != LoadingDrawCmd::Kind::Arc) continue;
            if (d.color.b > 0.5f) fill = &d;
            if (d.color.r > 0.5f) track = &d;
        }
        CHECK(track != nullptr && fill != nullptr);
        if (track) {
            CHECK_NEAR(track->u0, 0.0f, 1e-3f);
            CHECK_NEAR(track->u1, 6.2831853f, 1e-3f);
        }
        if (fill) {
            CHECK_NEAR(fill->u0, -1.5707963f, 1e-3f);
            CHECK_NEAR(fill->u1, -1.5707963f + 6.2831853f * 0.5f, 1e-3f);
            CHECK_NEAR(fill->x, 200.0f, 1e-2f);
            CHECK_NEAR(fill->y, 400.0f * 0.48f, 1e-2f);
        }
    }

    // Indeterminate bar: at time 0 the chunk sits at the left and is 30% of the track.
    {
        LoadingScreen ls;
        ls.layout = (int)LoadingScreen::Layout::Classic;
        ls.barStyle = (int)LoadingScreen::BarStyle::Indeterminate;
        ls.showBar = true;
        ls.showSpinner = false;
        ls.showPercent = false;
        ls.showTitle = false;
        ls.duration = 10.0f;
        ls.barFill = Color{0.02f, 0.8f, 0.03f, 1};
        ls.barBackground = Color{0.05f, 0.05f, 0.05f, 1};
        const float W = 1000.0f, H = 500.0f;
        const auto cmds = BuildLoadingScreen(ls, W, H, 0.0f);
        const float bw = W * ls.barWidth;
        const float bx = (W - bw) * 0.5f;
        const LoadingDrawCmd* chunk = nullptr;
        for (const auto& d : cmds)
            if (d.kind == LoadingDrawCmd::Kind::Rect && d.color.g > 0.5f) chunk = &d;
        CHECK(chunk != nullptr);
        if (chunk) {
            CHECK_NEAR(chunk->x, bx, 1e-2f);
            CHECK_NEAR(chunk->w, bw * 0.3f, 1e-2f);
            CHECK_NEAR(chunk->y, H * ls.barY, 1e-2f);
        }
    }

    // The bar colour is absent when the bar is hidden. The card draws a soft shadow.
    {
        LoadingScreen ls;
        ls.layout = (int)LoadingScreen::Layout::Classic;
        ls.showBar = false;
        ls.showSpinner = false;
        ls.showTitle = false;
        ls.barFill = Color{0.13f, 0.17f, 0.19f, 1};
        for (const auto& d : BuildLoadingScreen(ls, 800.0f, 600.0f, 0.0f))
            CHECK(std::fabs(d.color.r - 0.13f) > 0.01f || std::fabs(d.color.g - 0.17f) > 0.01f);

        LoadingScreen card;
        card.layout = (int)LoadingScreen::Layout::Card;
        card.showTitle = false;
        card.showSpinner = false;
        card.showBar = false;
        bool shadow = false;
        for (const auto& d : BuildLoadingScreen(card, 800.0f, 600.0f, 1.0f))
            if (d.kind == LoadingDrawCmd::Kind::Rect && d.color.r < 0.01f && d.color.g < 0.01f &&
                std::fabs(d.alpha - 0.35f) < 1e-3f)
                shadow = true;
        CHECK(shadow);
    }

    TEST_MAIN_RESULT();
}
