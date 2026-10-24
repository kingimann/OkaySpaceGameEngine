// Headless test for the LoadingScreen: show-on-start, progress, fade, tip rotation,
// reveal vs. scene-transition behaviour.
#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("loading_screen");

    Scene s("ls");
    GameObject* g = s.CreateGameObject("Loader");
    auto* ls = g->AddComponent<LoadingScreen>();
    ls->showOnStart = true; ls->duration = 2.0f; ls->fadeTime = 0.5f; ls->targetScene = "";
    ls->tips = {"alpha", "beta"};

    CHECK(!ls->Active());                       // nothing until the scene starts
    s.Start();
    CHECK(ls->Active());
    CHECK_NEAR(ls->Progress(), 0.0f, 1e-4f);
    CHECK(ls->CurrentTip() == "alpha");         // first tip
    CHECK(ls->Alpha() < 1.0f);                  // fading in at t=0

    s.Update(1.0f);
    CHECK_NEAR(ls->Progress(), 0.5f, 1e-3f);
    CHECK_NEAR(ls->Alpha(), 1.0f, 1e-3f);       // mid: fully opaque

    s.Update(1.2f);                             // past duration → reveal & hide
    CHECK(!ls->Active());

    ls->Begin();                                // re-show: tip rotates
    CHECK(ls->Active());
    CHECK(ls->CurrentTip() == "beta");

    // Transition mode: with a target scene it stays up after the bar fills (it has
    // requested the load, which swaps at end of frame — here the name is unknown so
    // it simply remains visible rather than vanishing).
    GameObject* g2 = s.CreateGameObject("Transition");
    auto* t = g2->AddComponent<LoadingScreen>();
    t->showOnStart = false; t->duration = 1.0f; t->targetScene = "NoSuchScene";
    t->Begin();
    CHECK(t->Active());
    s.Update(1.1f);
    CHECK(t->Active());                         // load requested, overlay stays up

    // ---- Layouts: every one lays out inside the screen, with its bar and text ----
    using K = LoadingDrawCmd::Kind;
    for (int li = 0; li < (int)LoadingScreen::Layout::Count; ++li) {
        for (float W : {1280.0f, 800.0f}) {
            const float H = W == 1280.0f ? 720.0f : 1280.0f;   // landscape and a tall phone
            Scene ps("p");
            auto* l = ps.CreateGameObject("L")->AddComponent<LoadingScreen>();
            ApplyLoadingPreset(*l, (LoadingScreen::Layout)li);
            l->title = "Dunes"; l->subtitle = "Chapter 1"; l->tips = {"tip one", "tip two"};
            l->showSpinner = true; l->showPercent = true; l->duration = 2.0f;
            ps.Start(); ps.Update(1.0f);
            const auto cmds = BuildLoadingScreen(*l, W, H);
            CHECK(!cmds.empty());
            int texts = 0, bars = 0;
            bool inside = true;
            for (const auto& d : cmds) {
                if (d.kind == K::Text) {
                    ++texts;
                    if (d.x < 0 || d.x > W || d.y < 0 || d.y + d.h > H) inside = false;
                }
                if (d.kind == K::Rect && d.w > 0 && d.h > 0 && (d.x < -0.5f || d.y < -0.5f || d.x + d.w > W + 0.5f || d.y + d.h > H + 0.5f)) inside = false;
                if ((d.kind == K::Rect && d.color.r == l->barFill.r && d.color.b == l->barFill.b && d.h < H * 0.05f) ||
                    (d.kind == K::Arc && d.color.b == l->barFill.b)) ++bars;
                CHECK(d.alpha >= 0.0f && d.alpha <= 1.0f);
            }
            if (!inside) std::cout << "  layout " << li << " at " << W << "x" << H << " leaves the screen\n";
            CHECK(inside);
            CHECK(texts >= 3);   // title, subtitle / tip, percent
            CHECK(bars >= 1);
        }
    }
    {   // The bar fills with progress; Segmented lights the right number of segments.
        Scene ps("p");
        auto* l = ps.CreateGameObject("L")->AddComponent<LoadingScreen>();
        l->duration = 2.0f; l->barStyle = (int)LoadingScreen::BarStyle::Segmented;
        l->barFill = Color::FromBytes(1, 2, 250);
        ps.Start(); ps.Update(1.0f);   // half way
        int lit = 0, segs = 0;
        for (const auto& d : BuildLoadingScreen(*l, 1280, 720))
            if (d.kind == K::Rect && d.h < 40 && d.w < 40) { ++segs; if (d.color.b > 0.9f) ++lit; }
        CHECK(segs == 20);
        CHECK(lit == 10);
    }
    {   // Preview mode (the Inspector) is opaque and works outside Play.
        LoadingScreen l; l.tips = {"a", "b"};
        const auto cmds = BuildLoadingScreen(l, 640, 360, 4.0f);
        CHECK(!cmds.empty());
        CHECK_NEAR(cmds.front().alpha, 1.0f, 1e-4f);
        bool tip = false;
        for (const auto& d : cmds) if (d.kind == LoadingDrawCmd::Kind::Text && (d.text == "a" || d.text == "b")) tip = true;
        CHECK(tip);
    }
    {   // Tips cycle while the screen is up when an interval is set.
        Scene ps("p");
        auto* l = ps.CreateGameObject("L")->AddComponent<LoadingScreen>();
        l->duration = 10.0f; l->tips = {"one", "two", "three"}; l->tipInterval = 1.0f;
        ps.Start();
        CHECK(l->CurrentTip() == "one");
        ps.Update(1.05f);
        CHECK(l->CurrentTip() == "two");
        ps.Update(1.05f);
        CHECK(l->CurrentTip() == "three");
    }
    {   // The new fields save and load.
        Scene ps("p");
        GameObject* go = ps.CreateGameObject("L");
        auto* l = go->AddComponent<LoadingScreen>();
        ApplyLoadingPreset(*l, LoadingScreen::Layout::Card);
        l->subtitle = "Chapter \"2\""; l->logoImage = "Assets/logo.png"; l->logoSize = 0.33f;
        l->logoPulse = false; l->tipInterval = 2.5f; l->spinnerStyle = (int)LoadingScreen::SpinnerStyle::Bars;
        l->barStyle = (int)LoadingScreen::BarStyle::Indeterminate; l->cardColor = Color::FromBytes(10, 20, 30);
        const std::string text = SceneSerializer::SerializeObject(*go);
        Scene ps2("q");
        GameObject* back = SceneSerializer::InstantiateFromText(ps2, text);
        CHECK(back != nullptr);
        auto* r = back ? back->GetComponent<LoadingScreen>() : nullptr;
        CHECK(r != nullptr);
        if (r) {
            CHECK(r->layout == (int)LoadingScreen::Layout::Card);
            CHECK(r->barStyle == (int)LoadingScreen::BarStyle::Indeterminate);
            CHECK(r->spinnerStyle == (int)LoadingScreen::SpinnerStyle::Bars);
            CHECK(r->subtitle == "Chapter \"2\"");
            CHECK(r->logoImage == "Assets/logo.png");
            CHECK_NEAR(r->logoSize, 0.33f, 1e-4f);
            CHECK(!r->logoPulse);
            CHECK(r->kenBurns == l->kenBurns);
            CHECK_NEAR(r->tipInterval, 2.5f, 1e-4f);
            CHECK_NEAR(r->cardColor.g, 20.0f / 255.0f, 1e-3f);
        }
    }

    TEST_MAIN_RESULT();
}
