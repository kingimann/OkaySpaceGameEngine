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

    TEST_MAIN_RESULT();
}
